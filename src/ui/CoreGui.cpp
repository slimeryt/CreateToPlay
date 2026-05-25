#include "CoreGui.h"
#include "embedded/EmbeddedFont.h"
#include "embedded/EmbeddedAssets.h"
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>

void CoreGui::Init(SDL_Window* window, SDL_GLContext glContext) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImFontConfig cfg;
    cfg.OversampleH          = 3;
    cfg.OversampleV          = 3;
    cfg.PixelSnapH           = false;
    cfg.FontDataOwnedByAtlas = false;  // we keep ownership of the static array

    // Body font — 17px from embedded TTF
    io.Fonts->AddFontFromMemoryTTF(
        (void*)kRobotoFontData, (int)kRobotoFontSize, 17.f, &cfg);

    // Title font — 30px
    m_fontTitle = io.Fonts->AddFontFromMemoryTTF(
        (void*)kRobotoFontData, (int)kRobotoFontSize, 30.f, &cfg);
    if (!m_fontTitle) m_fontTitle = io.Fonts->Fonts[0];

    ImGui::GetStyle().ScaleAllSizes(1.25f);

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // 3-D avatar preview FBO
    InitAvatarPreview();

    // Read server address for auth (same source as game connection)
    {
        std::string serverAddr;
        char* base = SDL_GetBasePath();
        if (base) {
            std::string cfgPath = std::string(base) + "assets/server.txt";
            SDL_free(base);
            if (FILE* f = fopen(cfgPath.c_str(), "r")) {
                char line[256] = {};
                if (fgets(line, sizeof(line), f)) {
                    for (char* p = line; *p; ++p)
                        if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
                    serverAddr = line;
                }
                fclose(f);
            }
        }
        if (serverAddr.empty()) serverAddr = kEmbeddedServerAddr;
        auto colon = serverAddr.rfind(':');
        if (colon != std::string::npos) {
            m_authHost = serverAddr.substr(0, colon);
            m_authPort = (uint16_t)std::stoi(serverAddr.substr(colon + 1));
        } else {
            m_authHost = serverAddr;
        }
    }

    // Session + avatar — load persisted data from AppData/pref directory
    {
        char* pref = SDL_GetPrefPath("CreateToPlay", "Game");
        if (pref) {
            m_sessionPath = std::string(pref) + "session.dat";
            m_avatarPath  = std::string(pref) + "avatar.dat";
            SDL_free(pref);

            // Auto-login
            if (FILE* f = fopen(m_sessionPath.c_str(), "r")) {
                char line[32] = {};
                if (fgets(line, sizeof(line), f) && line[0]) {
                    for (char* p = line; *p; ++p)
                        if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
                    if (line[0]) {
                        m_username = line;
                        m_loggedIn = true;
                    }
                }
                fclose(f);
            }

            // Restore saved avatar colours
            LoadAvatar();
        }
    }
}

void CoreGui::SaveSession() {
    if (m_sessionPath.empty()) return;
    if (FILE* f = fopen(m_sessionPath.c_str(), "w")) {
        fprintf(f, "%s\n", m_username.c_str());
        fclose(f);
    }
}

void CoreGui::ClearSession() {
    m_loggedIn  = false;
    m_username.clear();
    if (!m_sessionPath.empty()) remove(m_sessionPath.c_str());
}

void CoreGui::SaveAvatar() {
    if (m_avatarPath.empty()) return;
    if (FILE* f = fopen(m_avatarPath.c_str(), "w")) {
        fprintf(f, "%f %f %f\n", m_avatarSkin[0],  m_avatarSkin[1],  m_avatarSkin[2]);
        fprintf(f, "%f %f %f\n", m_avatarShirt[0], m_avatarShirt[1], m_avatarShirt[2]);
        fprintf(f, "%f %f %f\n", m_avatarPants[0], m_avatarPants[1], m_avatarPants[2]);
        fclose(f);
    }
}

void CoreGui::LoadAvatar() {
    if (m_avatarPath.empty()) return;
    if (FILE* f = fopen(m_avatarPath.c_str(), "r")) {
        fscanf(f, "%f %f %f", &m_avatarSkin[0],  &m_avatarSkin[1],  &m_avatarSkin[2]);
        fscanf(f, "%f %f %f", &m_avatarShirt[0], &m_avatarShirt[1], &m_avatarShirt[2]);
        fscanf(f, "%f %f %f", &m_avatarPants[0], &m_avatarPants[1], &m_avatarPants[2]);
        fclose(f);
        m_avatarDirty = true;  // apply to character on next FixedUpdate
    }
}

// ── 3-D avatar preview ────────────────────────────────────────────────────────

static GLuint AvatarCompileShader(const char* vsrc, const char* fsrc) {
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512] = {}; glGetShaderInfoLog(s, 512, nullptr, buf);
            printf("[AvatarPreview] Shader error: %s\n", buf);
        }
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER,   vsrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

void CoreGui::InitAvatarPreview() {
    // ── Shader ────────────────────────────────────────────────────────────────
    const char* kVS = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat3 uNorm;
out vec3 vN;
void main() {
    vN = normalize(uNorm * aNormal);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)glsl";

    const char* kFS = R"glsl(
#version 330 core
in vec3 vN;
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    vec3 L1 = normalize(vec3( 0.6,  1.0,  0.8));
    vec3 L2 = normalize(vec3(-0.4,  0.3, -0.6));
    float d1 = max(dot(vN, L1), 0.0);
    float d2 = max(dot(vN, L2), 0.0) * 0.25;
    vec3 c = (0.22 + 0.68*d1 + 0.10*d2) * uColor;
    FragColor = vec4(c, 1.0);
}
)glsl";

    m_avatarProg = AvatarCompileShader(kVS, kFS);

    // ── Cube mesh (unit cube, 24 verts × 6 floats, 36 indices) ───────────────
    static const float kV[] = {
        // Front (z+)
        -0.5f,-0.5f, 0.5f, 0,0,1,  0.5f,-0.5f, 0.5f, 0,0,1,
         0.5f, 0.5f, 0.5f, 0,0,1, -0.5f, 0.5f, 0.5f, 0,0,1,
        // Back (z-)
         0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f,-0.5f,-0.5f, 0,0,-1,
        -0.5f, 0.5f,-0.5f, 0,0,-1,  0.5f, 0.5f,-0.5f, 0,0,-1,
        // Left (x-)
        -0.5f,-0.5f,-0.5f,-1,0,0, -0.5f,-0.5f, 0.5f,-1,0,0,
        -0.5f, 0.5f, 0.5f,-1,0,0, -0.5f, 0.5f,-0.5f,-1,0,0,
        // Right (x+)
         0.5f,-0.5f, 0.5f, 1,0,0,  0.5f,-0.5f,-0.5f, 1,0,0,
         0.5f, 0.5f,-0.5f, 1,0,0,  0.5f, 0.5f, 0.5f, 1,0,0,
        // Top (y+)
        -0.5f, 0.5f, 0.5f, 0,1,0,  0.5f, 0.5f, 0.5f, 0,1,0,
         0.5f, 0.5f,-0.5f, 0,1,0, -0.5f, 0.5f,-0.5f, 0,1,0,
        // Bottom (y-)
        -0.5f,-0.5f,-0.5f, 0,-1,0,  0.5f,-0.5f,-0.5f, 0,-1,0,
         0.5f,-0.5f, 0.5f, 0,-1,0, -0.5f,-0.5f, 0.5f, 0,-1,0,
    };
    static const unsigned short kI[] = {
         0, 1, 2,  2, 3, 0,
         4, 5, 6,  6, 7, 4,
         8, 9,10, 10,11, 8,
        12,13,14, 14,15,12,
        16,17,18, 18,19,16,
        20,21,22, 22,23,20,
    };

    glGenVertexArrays(1, (GLuint*)&m_avatarVAO);
    glGenBuffers(1, (GLuint*)&m_avatarVBO);
    glGenBuffers(1, (GLuint*)&m_avatarEBO);

    glBindVertexArray((GLuint)m_avatarVAO);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)m_avatarVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kV), kV, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)m_avatarEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kI), kI, GL_STATIC_DRAW);
    // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glBindVertexArray(0);

    // ── FBO ───────────────────────────────────────────────────────────────────
    glGenFramebuffers(1, (GLuint*)&m_avatarFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_avatarFBO);

    // Colour texture
    glGenTextures(1, (GLuint*)&m_avatarTex);
    glBindTexture(GL_TEXTURE_2D, (GLuint)m_avatarTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kAvatarFBOW, kAvatarFBOH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)m_avatarTex, 0);

    // Depth renderbuffer
    glGenRenderbuffers(1, (GLuint*)&m_avatarDepthRB);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)m_avatarDepthRB);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kAvatarFBOW, kAvatarFBOH);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, (GLuint)m_avatarDepthRB);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("[AvatarPreview] FBO incomplete!\n");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CoreGui::ShutdownAvatarPreview() {
    if (m_avatarFBO)     { glDeleteFramebuffers(1,  (GLuint*)&m_avatarFBO);     m_avatarFBO     = 0; }
    if (m_avatarTex)     { glDeleteTextures(1,       (GLuint*)&m_avatarTex);     m_avatarTex     = 0; }
    if (m_avatarDepthRB) { glDeleteRenderbuffers(1,  (GLuint*)&m_avatarDepthRB); m_avatarDepthRB = 0; }
    if (m_avatarVAO)     { glDeleteVertexArrays(1,   (GLuint*)&m_avatarVAO);     m_avatarVAO     = 0; }
    if (m_avatarVBO)     { glDeleteBuffers(1,         (GLuint*)&m_avatarVBO);     m_avatarVBO     = 0; }
    if (m_avatarEBO)     { glDeleteBuffers(1,         (GLuint*)&m_avatarEBO);     m_avatarEBO     = 0; }
    if (m_avatarProg)    { glDeleteProgram((GLuint)m_avatarProg);                  m_avatarProg    = 0; }
}

void CoreGui::RenderAvatarPreview() {
    if (!m_avatarFBO || !m_avatarProg) return;

    // ── Save GL state ─────────────────────────────────────────────────────────
    GLint  prevFBO = 0, prevProg = 0, prevVAO = 0;
    GLint  prevVP[4] = {};
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCull  = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,  &prevFBO);
    glGetIntegerv(GL_VIEWPORT,              prevVP);
    glGetIntegerv(GL_CURRENT_PROGRAM,      &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);

    // ── Render to FBO ─────────────────────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_avatarFBO);
    glViewport(0, 0, kAvatarFBOW, kAvatarFBOH);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.08f, 0.08f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram((GLuint)m_avatarProg);
    glBindVertexArray((GLuint)m_avatarVAO);

    // Camera — portrait-fit of R6 body
    float aspect = (float)kAvatarFBOW / (float)kAvatarFBOH;
    glm::mat4 proj = glm::perspective(glm::radians(45.f), aspect, 0.1f, 50.f);
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.f,  0.04f, 7.f),
        glm::vec3(0.f,  0.04f, 0.f),
        glm::vec3(0.f,  1.f,   0.f));
    glm::mat4 vp = proj * view;

    // Slow auto-spin (full rotation every ~8 s, starts at 25° so 3/4 view on open)
    float yaw = 0.436f + (float)(fmod(ImGui::GetTime() * 0.785, 6.2832));
    glm::mat4 rootRot = glm::rotate(glm::mat4(1.f), yaw, glm::vec3(0,1,0));

    GLint locMVP   = glGetUniformLocation((GLuint)m_avatarProg, "uMVP");
    GLint locNorm  = glGetUniformLocation((GLuint)m_avatarProg, "uNorm");
    GLint locColor = glGetUniformLocation((GLuint)m_avatarProg, "uColor");

    struct Part { glm::vec3 off; glm::vec3 sz; int ci; };
    const Part parts[] = {
        {{ 0.f,     0.14f,  0.f},  {1.40f,1.40f,0.70f}, 1}, // torso  (shirt)
        {{ 0.f,     1.51f,  0.f},  {1.20f,1.20f,1.20f}, 0}, // head   (skin)
        {{-1.12f,   0.14f,  0.f},  {0.70f,1.40f,0.70f}, 0}, // L arm  (skin)
        {{ 1.12f,   0.14f,  0.f},  {0.70f,1.40f,0.70f}, 0}, // R arm  (skin)
        {{-0.385f, -1.33f,  0.f},  {0.63f,1.40f,0.70f}, 2}, // L leg  (pants)
        {{ 0.385f, -1.33f,  0.f},  {0.63f,1.40f,0.70f}, 2}, // R leg  (pants)
    };
    const glm::vec3 colors[3] = {
        {m_avatarSkin[0],  m_avatarSkin[1],  m_avatarSkin[2] },
        {m_avatarShirt[0], m_avatarShirt[1], m_avatarShirt[2]},
        {m_avatarPants[0], m_avatarPants[1], m_avatarPants[2]},
    };

    // Draw back-to-front isn't needed (depth test handles it), just draw all
    for (auto& p : parts) {
        glm::vec3 wpos  = glm::vec3(rootRot * glm::vec4(p.off, 1.f));
        glm::mat4 model = glm::translate(glm::mat4(1.f), wpos)
                        * glm::rotate(glm::mat4(1.f), yaw, glm::vec3(0,1,0))
                        * glm::scale(glm::mat4(1.f), p.sz);
        glm::mat4 mvp   = vp * model;
        glm::mat3 norm  = glm::mat3(glm::transpose(glm::inverse(model)));
        glUniformMatrix4fv(locMVP,  1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix3fv(locNorm, 1, GL_FALSE, glm::value_ptr(norm));
        glUniform3fv(locColor, 1, glm::value_ptr(colors[p.ci]));
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, nullptr);
    }

    // ── Restore GL state ──────────────────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
    glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
    glUseProgram((GLuint)prevProg);
    glBindVertexArray((GLuint)prevVAO);
    if (!prevDepth) glDisable(GL_DEPTH_TEST);
    if (!prevCull)  glDisable(GL_CULL_FACE);
    if  (prevBlend) glEnable(GL_BLEND);
}

void CoreGui::Shutdown() {
    ShutdownAvatarPreview();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void CoreGui::ProcessEvent(const SDL_Event& e) {
    ImGui_ImplSDL2_ProcessEvent(&e);
}

void CoreGui::BeginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void CoreGui::Render() {
    if (m_menuOpen) {
        DrawEscapeMenu();

        if (m_leaveConfirmOpen) {
            // Confirm mode: Enter = leave, Esc = cancel
            if (ImGui::IsKeyPressed(ImGuiKey_Enter,  false)) m_wantsLeave       = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !m_skipEscapeThisFrame)
                m_leaveConfirmOpen = false;
        } else {
            // Normal mode keybinds
            if (ImGui::IsKeyPressed(ImGuiKey_L,      false)) m_leaveConfirmOpen = true;
            if (ImGui::IsKeyPressed(ImGuiKey_R,      false)) m_wantsReset       = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !m_skipEscapeThisFrame)
                m_menuOpen = false;
        }
        m_skipEscapeThisFrame = false;
    }

    // ── Nametags (drawn before HUD so HUD stays on top) ─────────────────────
    if (!m_nametags.empty()) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({0.f, 0.f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.f, 0.f, 0.f, 0.f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
        ImGui::Begin("##nametags", nullptr,
            ImGuiWindowFlags_NoDecoration  | ImGuiWindowFlags_NoInputs  |
            ImGuiWindowFlags_NoNav          | ImGuiWindowFlags_NoMove    |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* ndl = ImGui::GetWindowDrawList();
        const float padX = 9.f, padY = 4.f, rounding = 5.f;

        for (const auto& tag : m_nametags) {
            if (tag.name.empty()) continue;
            ImVec2 tsz = ImGui::CalcTextSize(tag.name.c_str());
            // Centre the tag horizontally, place it above the projected head point
            float px = tag.x - tsz.x * 0.5f;
            float py = tag.y - tsz.y - padY * 2.f - 4.f;

            // Dark semi-transparent pill
            ndl->AddRectFilled(
                {px - padX,          py - padY},
                {px + tsz.x + padX,  py + tsz.y + padY},
                IM_COL32(10, 10, 18, 175), rounding);
            ndl->AddRect(
                {px - padX,          py - padY},
                {px + tsz.x + padX,  py + tsz.y + padY},
                IM_COL32(80, 120, 255, 90), rounding, 0, 0.9f);

            // White name text
            ndl->AddText({px, py}, IM_COL32(235, 235, 255, 230), tag.name.c_str());
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    DrawMenuButton(); // always on top

    // ── Connection status pill (top-right) ────────────────────────────────────
    {
        ImGuiIO& io = ImGui::GetIO();
        const char* label = m_connected ? "Online" : "Offline";
        // Format label with player count when connected
        char buf[32];
        if (m_connected)
            snprintf(buf, sizeof(buf), "Online  %d", m_playerCount);
        else
            snprintf(buf, sizeof(buf), "Offline");

        ImVec2 tsz    = ImGui::CalcTextSize(buf);
        float  dotR   = 5.f;
        float  padX   = 10.f, padY = 6.f;
        float  pillW  = dotR * 2.f + 6.f + tsz.x + padX * 2.f;
        float  pillH  = tsz.y + padY * 2.f;
        float  px     = io.DisplaySize.x - pillW - 12.f;
        float  py     = 12.f;

        ImGui::SetNextWindowPos({px, py});
        ImGui::SetNextWindowSize({pillW, pillH});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
        ImGui::Begin("##netstatus", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove   |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wpos = ImGui::GetWindowPos();

        // Pill background
        ImU32 bgCol = m_connected ? IM_COL32(10, 28, 10, 200) : IM_COL32(20, 20, 20, 180);
        dl->AddRectFilled(wpos, {wpos.x + pillW, wpos.y + pillH},
            bgCol, pillH * 0.5f);
        dl->AddRect(wpos, {wpos.x + pillW, wpos.y + pillH},
            m_connected ? IM_COL32(40, 160, 40, 120) : IM_COL32(80, 80, 80, 100),
            pillH * 0.5f, 0, 1.f);

        // Dot
        ImU32 dotCol = m_connected ? IM_COL32(60, 220, 80, 255) : IM_COL32(130, 130, 130, 255);
        float dotCX  = wpos.x + padX + dotR;
        float dotCY  = wpos.y + pillH * 0.5f;
        dl->AddCircleFilled({dotCX, dotCY}, dotR, dotCol);

        // Text
        ImGui::SetCursorPos({padX + dotR * 2.f + 6.f, padY});
        ImGui::TextColored(
            m_connected ? ImVec4(0.7f, 1.f, 0.7f, 1.f) : ImVec4(0.6f, 0.6f, 0.6f, 1.f),
            "%s", buf);

        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Top-left menu button (drawn with primitives — no font dependency) ─────────

void CoreGui::DrawMenuButton() {
    const float kSize = 44.f;
    const float kPad  = 10.f;

    // Transparent host window just to anchor the hit-test area
    ImGui::SetNextWindowPos({kPad, kPad});
    ImGui::SetNextWindowSize({kSize, kSize});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.f, 0.f));
    ImGui::Begin("##menubtn", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Record top-left before the invisible button moves the cursor
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##hit", ImVec2(kSize, kSize));
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    if (clicked) m_menuOpen = !m_menuOpen;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background — black, blue tint on hover/active/open
    ImU32 bgCol = active      ? IM_COL32( 27,  92, 240, 255) :
                  hovered     ? IM_COL32( 18,  18,  26, 245) :
                  m_menuOpen  ? IM_COL32( 20,  60, 160, 230) :
                                IM_COL32(  8,   8,  12, 210);
    dl->AddRectFilled(p, ImVec2(p.x + kSize, p.y + kSize), bgCol, 10.f);

    // Subtle blue border
    ImU32 borderCol = m_menuOpen ? IM_COL32(27, 92, 240, 180) : IM_COL32(50, 50, 80, 140);
    dl->AddRect(p, ImVec2(p.x + kSize, p.y + kSize), borderCol, 10.f, 0, 1.2f);

    ImU32 lineCol = IM_COL32(240, 240, 255, 248);
    const float margin = kSize * 0.28f;

    if (m_menuOpen) {
        // X — two diagonal lines
        ImVec2 tl = { p.x + margin,         p.y + margin         };
        ImVec2 br = { p.x + kSize - margin,  p.y + kSize - margin };
        ImVec2 tr = { p.x + kSize - margin,  p.y + margin         };
        ImVec2 bl = { p.x + margin,          p.y + kSize - margin };
        dl->AddLine(tl, br, lineCol, 2.8f);
        dl->AddLine(tr, bl, lineCol, 2.8f);
    } else {
        // Three hamburger lines
        const float lineW  = kSize * 0.52f;
        const float lineH  = 2.8f;
        const float gap    = 5.5f;
        const float totalH = lineH * 3.f + gap * 2.f;
        float lx = p.x + (kSize - lineW) * 0.5f;
        float ly = p.y + (kSize - totalH) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            float y = ly + i * (lineH + gap);
            dl->AddRectFilled(ImVec2(lx, y), ImVec2(lx + lineW, y + lineH), lineCol, 1.4f);
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Escape / pause menu ───────────────────────────────────────────────────────

void CoreGui::DrawEscapeMenu() {
    ImGuiIO& io = ImGui::GetIO();

    // Full-screen dim overlay — deep black
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.60f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##overlay", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs      |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove         |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Panel — scales with window
    const float pw = std::max(620.f, std::min(1100.f, io.DisplaySize.x * 0.70f));
    const float ph = std::max(400.f, std::min( 700.f, io.DisplaySize.y * 0.62f));
    ImGui::SetNextWindowPos({
        (io.DisplaySize.x - pw) * 0.5f,
        (io.DisplaySize.y - ph) * 0.5f
    });
    ImGui::SetNextWindowSize({pw, ph});

    const float kPad  = 28.f;
    const float kBotH = 76.f;
    const float kBtnH = 42.f;

    // ── Palette ───────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.06f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.11f, 0.11f, 0.18f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   7.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(kPad, kPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(0.f, 0.f));

    ImGui::Begin("##menu", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoSavedSettings);

    // ── Title — white, large, centered ───────────────────────────────────────
    ImGui::PushFont(m_fontTitle);
    const char* title = "CREATETOPLAY";
    ImGui::SetCursorPosX((pw - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextColored(ImVec4(1.f, 1.f, 1.f, 1.f), "%s", title);
    ImGui::PopFont();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.f);
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.f, 1.f, 1.f, 0.60f)); // blue separator
    ImGui::Separator();
    ImGui::PopStyleColor();

    // ── Bottom action bar ─────────────────────────────────────────────────────
    ImGui::SetCursorPosY(ph - kBotH);

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.f, 1.f, 1.f, 0.55f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.f);

    float avail = pw - kPad * 2.f;
    float gap   = 10.f;

    if (m_leaveConfirmOpen) {
        // ── Confirm bar: Cancel (left) + Leave (right) ────────────────────────
        float btnW = (avail - gap) * 0.5f;

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.10f, 0.15f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.24f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.08f, 0.08f, 0.12f, 1.f));
        if (ImGui::Button("Cancel  [Esc]", ImVec2(btnW, kBtnH)))
            m_leaveConfirmOpen = false;
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, gap);

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.11f, 0.36f, 0.94f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.48f, 1.00f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.07f, 0.26f, 0.80f, 1.f));
        if (ImGui::Button("Leave  [Enter]", ImVec2(btnW, kBtnH)))
            m_wantsLeave = true;
        ImGui::PopStyleColor(3);
    } else {
        // ── Normal bar: Leave | Reset Character | Resume ──────────────────────
        float btnW = (avail - gap * 2.f) / 3.f;

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.07f, 0.07f, 0.10f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.11f, 0.11f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.05f, 0.05f, 0.08f, 1.f));
        if (ImGui::Button("Leave  [L]", ImVec2(btnW, kBtnH)))
            m_leaveConfirmOpen = true;
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, gap);

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.10f, 0.15f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.24f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.08f, 0.08f, 0.12f, 1.f));
        if (ImGui::Button("Reset Character  [R]", ImVec2(btnW, kBtnH)))
            m_wantsReset = true;
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, gap);

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.11f, 0.36f, 0.94f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.48f, 1.00f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.07f, 0.26f, 0.80f, 1.f));
        if (ImGui::Button("Resume  [Esc]", ImVec2(btnW, kBtnH)))
            m_menuOpen = false;
        ImGui::PopStyleColor(3);
    }

    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

// ── Home page ─────────────────────────────────────────────────────────────────

void CoreGui::RenderHomePage() {
    DrawHomePage();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void CoreGui::DrawHomePage() {
    if (!m_loggedIn) {
        DrawAuthScreen();
        return;
    }

    ImGuiIO& io   = ImGui::GetIO();
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;

    // ── Full-screen background window ─────────────────────────────────────────
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize({W, H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.10f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
    ImGui::Begin("##home", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl       = ImGui::GetWindowDrawList();
    const float sideW    = 76.f;
    const float btnSlotH = 76.f;

    // ── Sidebar ───────────────────────────────────────────────────────────────
    dl->AddRectFilled({0.f, 0.f}, {sideW, H}, IM_COL32(8, 8, 14, 255));
    dl->AddLine({sideW, 0.f}, {sideW, H}, IM_COL32(38, 38, 60, 200), 1.f);

    // Logo — "C2P" monogram centered in sidebar at the top
    {
        const char* mono = "C2P";
        ImVec2 tsz = ImGui::CalcTextSize(mono);
        float lx = (sideW - tsz.x) * 0.5f;
        float ly = 14.f;
        // Pill background
        ImVec2 pillTL = {lx - 7.f, ly - 4.f};
        ImVec2 pillBR = {lx + tsz.x + 7.f, ly + tsz.y + 4.f};
        dl->AddRectFilled(pillTL, pillBR, IM_COL32(28, 92, 240, 200), 7.f);
        ImGui::SetCursorPos({lx, ly});
        ImGui::TextColored({1.f, 1.f, 1.f, 1.f}, "%s", mono);
    }

    struct SideItem { const char* label; };
    static const SideItem kItems[] = { {"Home"}, {"Avatar"}, {"More"} };

    // Push nav items down below the logo
    const float navOffsetY = 58.f;

    for (int i = 0; i < 3; ++i) {
        float by = navOffsetY + i * (btnSlotH + 4.f);

        ImGui::SetCursorPos({0.f, by});
        char bid[16]; snprintf(bid, sizeof(bid), "##sb%d", i);
        bool clicked = ImGui::InvisibleButton(bid, {sideW, btnSlotH});
        bool hovered = ImGui::IsItemHovered();
        if (clicked) m_sidebarTab = i;
        bool active  = (m_sidebarTab == i);

        // Hover / active tint
        if (active)
            dl->AddRectFilled({0.f, by}, {sideW, by + btnSlotH},
                IM_COL32(28, 92, 240, 28));
        else if (hovered)
            dl->AddRectFilled({0.f, by}, {sideW, by + btnSlotH},
                IM_COL32(255, 255, 255, 12));

        // Left accent bar — same height as background
        if (active)
            dl->AddRectFilled({0.f, by}, {3.f, by + btnSlotH},
                IM_COL32(28, 92, 240, 255), 2.f);

        ImU32 iconCol = active ? IM_COL32(80, 150, 255, 255)
                               : IM_COL32(170, 170, 185, 255);
        float cx = sideW * 0.5f;
        float iy = by + 12.f;

        if (i == 0) {
            // House: roof triangle + body rect + door cutout
            dl->AddTriangleFilled({cx, iy}, {cx - 12.f, iy + 11.f}, {cx + 12.f, iy + 11.f}, iconCol);
            dl->AddRectFilled({cx - 8.f, iy + 11.f}, {cx + 8.f, iy + 24.f}, iconCol, 1.f);
            dl->AddRectFilled({cx - 3.5f, iy + 16.f}, {cx + 3.5f, iy + 24.f}, IM_COL32(8, 8, 14, 255));
        } else if (i == 1) {
            // Person: circle head + rounded body
            dl->AddCircleFilled({cx, iy + 6.f}, 5.5f, iconCol);
            dl->AddRectFilled({cx - 7.f, iy + 13.f}, {cx + 7.f, iy + 24.f}, iconCol, 3.f);
        } else {
            // Three horizontal dots
            for (int d = 0; d < 3; ++d)
                dl->AddCircleFilled({cx - 8.f + d * 8.f, iy + 12.f}, 2.8f, iconCol);
        }

        // Label
        float lw = ImGui::CalcTextSize(kItems[i].label).x;
        ImGui::SetCursorPos({(sideW - lw) * 0.5f, by + 38.f});
        ImGui::PushStyleColor(ImGuiCol_Text,
            active ? ImVec4(0.50f, 0.72f, 1.f, 1.f) : ImVec4(0.62f, 0.62f, 0.72f, 1.f));
        ImGui::TextUnformatted(kItems[i].label);
        ImGui::PopStyleColor();
    }

    // ── Username chip at the bottom of the sidebar (display only) ───────────
    {
        const float chipH = 44.f;
        const float chipY = H - chipH - 10.f;
        const float chipX = 6.f;
        const float chipW = sideW - 12.f;

        dl->AddRectFilled({chipX, chipY}, {chipX + chipW, chipY + chipH},
            IM_COL32(20, 20, 32, 200), 8.f);
        dl->AddRect({chipX, chipY}, {chipX + chipW, chipY + chipH},
            IM_COL32(50, 50, 80, 140), 8.f, 0, 1.f);

        // Avatar initial circle
        float cx2 = chipX + 18.f;
        float cy2 = chipY + chipH * 0.5f;
        dl->AddCircleFilled({cx2, cy2}, 11.f, IM_COL32(28, 92, 240, 200));
        char init[2] = { (char)toupper((unsigned char)m_username[0]), '\0' };
        ImVec2 itsz = ImGui::CalcTextSize(init);
        dl->AddText({cx2 - itsz.x * 0.5f, cy2 - itsz.y * 0.5f},
            IM_COL32(255, 255, 255, 255), init);

        // Truncated username label
        std::string display = m_username;
        if (display.size() > 8) display = display.substr(0, 7) + ".";
        float nw = ImGui::CalcTextSize(display.c_str()).x;
        float nx = cx2 + 14.f;
        if (nx + nw < chipX + chipW - 4.f)
            dl->AddText({nx, cy2 - ImGui::GetTextLineHeight() * 0.5f},
                IM_COL32(200, 200, 220, 255), display.c_str());
    }

    // ── Content area ──────────────────────────────────────────────────────────
    ImGui::SetCursorPos({sideW, 0.f});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.f, 0.f, 0.f, 0.f});
    ImGui::BeginChild("##content", {W - sideW, H}, false);
    ImDrawList* cdl = ImGui::GetWindowDrawList();

    const float padX = 40.f;

    if (m_sidebarTab == 0) {
        // ── Home tab ──────────────────────────────────────────────────────────
        ImGui::SetCursorPos({padX, 30.f});
        ImGui::PushFont(m_fontTitle);
        ImGui::TextColored({0.88f, 0.88f, 0.94f, 1.f}, "CreateToPlay");
        ImGui::PopFont();

        // Single game card
        const float cw     = 280.f;
        const float thumbH = cw * 0.58f;
        const float infoH  = 82.f;   // name + play button
        const float ch     = thumbH + infoH;
        const float corner = 10.f;
        const float cardY  = 30.f + ImGui::GetTextLineHeight() + 22.f;

        ImGui::SetCursorPos({padX, cardY});
        ImVec2 tl = ImGui::GetCursorScreenPos();
        ImVec2 br = {tl.x + cw, tl.y + ch};

        bool cardHov = ImGui::IsMouseHoveringRect(tl, br);

        cdl->AddRectFilled(tl, br, IM_COL32(13, 13, 20, 255), corner);
        cdl->AddRect      (tl, br,
            cardHov ? IM_COL32(60, 60, 95, 220) : IM_COL32(40, 40, 65, 180),
            corner, 0, 1.f);

        // Thumbnail (subtle dim on hover to hint interactivity)
        ImVec2 thumbBR = {tl.x + cw, tl.y + thumbH};
        cdl->AddRectFilled(tl, thumbBR,
            IM_COL32(46, 107, 183, 255), corner, ImDrawFlags_RoundCornersTop);
        if (cardHov)
            cdl->AddRectFilled(tl, thumbBR,
                IM_COL32(0, 0, 0, 60), corner, ImDrawFlags_RoundCornersTop);

        // Game name (always visible)
        ImGui::SetCursorPos({padX + 12.f, cardY + thumbH + 12.f});
        ImGui::TextColored({1.f, 1.f, 1.f, 1.f}, "Test");

        // Play button — appears on hover
        if (cardHov) {
            float pbW   = cw - 24.f;
            float pbH   = 34.f;
            float pbX   = tl.x + 12.f;
            float pbY   = tl.y + thumbH + infoH - pbH - 12.f;
            ImVec2 pbTL = {pbX, pbY};
            ImVec2 pbBR = {pbX + pbW, pbY + pbH};

            bool btnHov = ImGui::IsMouseHoveringRect(pbTL, pbBR);
            cdl->AddRectFilled(pbTL, pbBR,
                btnHov ? IM_COL32(40, 110, 255, 240) : IM_COL32(28, 92, 240, 220), 7.f);

            // Play triangle centered in button
            float cx = pbX + pbW * 0.5f;
            float cy = pbY + pbH * 0.5f;
            float ts = 8.f;
            cdl->AddTriangleFilled(
                {cx - ts * 0.55f, cy - ts * 0.85f},
                {cx - ts * 0.55f, cy + ts * 0.85f},
                {cx + ts * 1.0f,  cy},
                IM_COL32(255, 255, 255, 255));

            if (btnHov && ImGui::IsMouseClicked(0))
                m_gameStarted = true;
        }

    } else if (m_sidebarTab == 1) {
        // ── Avatar tab ────────────────────────────────────────────────────────
        ImGui::SetCursorPos({padX, 30.f});
        ImGui::PushFont(m_fontTitle);
        ImGui::TextColored({0.88f, 0.88f, 0.94f, 1.f}, "Avatar");
        ImGui::PopFont();

        const float contentW = W - sideW;
        const float previewW = 200.f;
        const float editX    = sideW + previewW + 20.f;
        const float editW    = contentW - previewW - 40.f;
        const float topY     = 30.f + ImGui::GetTextLineHeight() + 22.f;

        // ── 3-D character preview (FBO texture) ───────────────────────────────
        {
            // Render the avatar to the off-screen FBO (safe here — before ImGui::Render)
            RenderAvatarPreview();

            // Display the FBO colour texture as an ImGui image.
            // UV is flipped vertically (OpenGL origin = bottom-left, ImGui = top-left).
            const float dispW = previewW - 20.f;
            const float dispH = dispW * (float)kAvatarFBOH / (float)kAvatarFBOW;

            // Dark card behind the image
            ImVec2 cardTL = {10.f, topY - 4.f};
            ImVec2 cardBR = {10.f + previewW - 20.f, topY + dispH + 4.f};
            cdl->AddRectFilled(cardTL, cardBR, IM_COL32(12, 12, 18, 255), 8.f);
            cdl->AddRect      (cardTL, cardBR, IM_COL32(40, 40, 65, 180),  8.f, 0, 1.f);

            ImGui::SetCursorPos({10.f, topY});
            ImGui::Image(
                (ImTextureID)(intptr_t)(unsigned int)m_avatarTex,
                {dispW, dispH},
                {0.f, 1.f}, {1.f, 0.f});   // flip Y for OpenGL→ImGui
        }

        // ── Colour editors ────────────────────────────────────────────────────
        // Preset swatch colours
        static const ImVec4 kSkinPresets[] = {
            {0.976f, 0.820f, 0.173f, 1.f}, // noob yellow
            {1.000f, 0.800f, 0.620f, 1.f}, // light skin
            {0.870f, 0.670f, 0.450f, 1.f}, // medium skin
            {0.550f, 0.360f, 0.210f, 1.f}, // dark skin
            {0.300f, 0.700f, 0.950f, 1.f}, // cyan
            {0.900f, 0.300f, 0.600f, 1.f}, // pink
        };
        static const ImVec4 kShirtPresets[] = {
            {0.059f, 0.420f, 0.690f, 1.f}, // noob blue
            {0.820f, 0.120f, 0.120f, 1.f}, // red
            {0.100f, 0.600f, 0.220f, 1.f}, // green
            {0.860f, 0.680f, 0.080f, 1.f}, // yellow
            {0.540f, 0.140f, 0.760f, 1.f}, // purple
            {0.880f, 0.440f, 0.090f, 1.f}, // orange
            {0.120f, 0.120f, 0.140f, 1.f}, // black
            {0.900f, 0.900f, 0.920f, 1.f}, // white
        };
        static const ImVec4 kPantsPresets[] = {
            {0.110f, 0.529f, 0.047f, 1.f}, // noob green
            {0.100f, 0.180f, 0.480f, 1.f}, // dark blue
            {0.160f, 0.160f, 0.180f, 1.f}, // dark grey
            {0.600f, 0.180f, 0.100f, 1.f}, // maroon
            {0.400f, 0.280f, 0.150f, 1.f}, // brown
            {0.300f, 0.550f, 0.600f, 1.f}, // teal
            {0.120f, 0.120f, 0.140f, 1.f}, // black
            {0.820f, 0.820f, 0.840f, 1.f}, // light grey
        };

        // Helper: draw a row of colour swatches; clicking one writes into dst[3]
        auto DrawSwatches = [&](const char* id, float* dst, const ImVec4* presets, int n) {
            ImGui::PushID(id);
            const float sz = 22.f;
            const float gap = 4.f;
            for (int k = 0; k < n; ++k) {
                if (k > 0) ImGui::SameLine(0.f, gap);
                ImGui::PushID(k);
                ImVec4 col = presets[k];
                bool isCurrent = (fabsf(dst[0]-col.x)<0.01f &&
                                  fabsf(dst[1]-col.y)<0.01f &&
                                  fabsf(dst[2]-col.z)<0.01f);
                if (isCurrent)
                    ImGui::PushStyleColor(ImGuiCol_Button, col);
                else
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(col.x*0.7f, col.y*0.7f, col.z*0.7f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImVec4(col.x*0.85f, col.y*0.85f, col.z*0.85f, 1.f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
                if (ImGui::Button("##sw", {sz, sz})) {
                    dst[0] = col.x; dst[1] = col.y; dst[2] = col.z;
                    m_avatarDirty = true;
                    SaveAvatar();
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
            ImGui::PopID();
        };

        struct ColorRow { const char* label; float* col; const ImVec4* presets; int n; };
        ColorRow rows[] = {
            { "Skin  ",  m_avatarSkin,  kSkinPresets,  6 },
            { "Shirt ",  m_avatarShirt, kShirtPresets, 8 },
            { "Pants ",  m_avatarPants, kPantsPresets, 8 },
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8.f, 16.f});

        float rowY = topY;
        for (auto& row : rows) {
            ImGui::SetCursorPos({editX - sideW, rowY});

            // Label
            ImGui::TextColored({0.70f, 0.70f, 0.80f, 1.f}, "%s", row.label);
            ImGui::SameLine(0.f, 12.f);

            // Full colour picker button
            ImGui::PushID(row.label);
            float prevR = row.col[0], prevG = row.col[1], prevB = row.col[2];
            if (ImGui::ColorEdit3("##col", row.col,
                    ImGuiColorEditFlags_NoLabel |
                    ImGuiColorEditFlags_NoSidePreview |
                    ImGuiColorEditFlags_PickerHueWheel)) {
                m_avatarDirty = true;
                SaveAvatar();
            }
            ImGui::PopID();

            // Presets row below
            ImGui::SetCursorPos({editX - sideW, rowY + 30.f});
            DrawSwatches(row.label, row.col, row.presets, row.n);
            rowY += 80.f;
        }

        ImGui::PopStyleVar(2);

    } else {
        // ── More / Options tab ────────────────────────────────────────────────
        ImGui::SetCursorPos({padX, 30.f});
        ImGui::PushFont(m_fontTitle);
        ImGui::TextColored({0.88f, 0.88f, 0.94f, 1.f}, "Options");
        ImGui::PopFont();

        struct OptionCard { const char* label; };
        static const OptionCard kOpts[] = { {"Settings"}, {"Profile"} };

        const float csz    = 128.f;
        const float corner = 14.f;
        const float gapX   = 18.f;
        const float cardY  = 30.f + ImGui::GetTextLineHeight() + 22.f;

        for (int i = 0; i < 2; ++i) {
            float lx = padX + i * (csz + gapX);
            ImGui::SetCursorPos({lx, cardY});
            ImVec2 tl = ImGui::GetCursorScreenPos();
            ImVec2 br = {tl.x + csz, tl.y + csz};

            bool hov = ImGui::IsMouseHoveringRect(tl, br);

            cdl->AddRectFilled(tl, br,
                hov ? IM_COL32(20, 20, 30, 255) : IM_COL32(13, 13, 20, 255), corner);
            cdl->AddRect(tl, br,
                hov ? IM_COL32(70, 70, 110, 220) : IM_COL32(40, 40, 65, 180),
                corner, 0, 1.2f);

            float cx  = tl.x + csz * 0.5f;
            float icy = tl.y + csz * 0.34f;
            ImU32 ic  = hov ? IM_COL32(100, 160, 255, 255) : IM_COL32(160, 160, 180, 255);

            if (i == 0) {
                for (int s = 0; s < 3; ++s) {
                    float ly2 = icy - 10.f + s * 10.f;
                    cdl->AddRectFilled({cx - 18.f, ly2 - 1.5f}, {cx + 18.f, ly2 + 1.5f}, ic, 2.f);
                    float kx = cx - 10.f + s * 10.f;
                    cdl->AddCircleFilled({kx, ly2}, 4.f, IM_COL32(13, 13, 20, 255));
                    cdl->AddCircle      ({kx, ly2}, 4.f, ic, 12, 1.5f);
                }
            } else {
                cdl->AddCircleFilled({cx, icy - 8.f}, 9.f, ic);
                cdl->AddRectFilled({cx - 11.f, icy + 3.f}, {cx + 11.f, icy + 18.f}, ic, 5.f);
            }

            float lw2 = ImGui::CalcTextSize(kOpts[i].label).x;
            ImGui::SetCursorPos({lx + (csz - lw2) * 0.5f, cardY + csz * 0.72f});
            ImGui::TextColored(
                hov ? ImVec4(1.f, 1.f, 1.f, 1.f) : ImVec4(0.65f, 0.65f, 0.75f, 1.f),
                "%s", kOpts[i].label);
        }

        // ── Sign Out button ───────────────────────────────────────────────────
        {
            const float btnW = csz * 2.f + gapX;
            const float btnH = 44.f;
            const float btnY = cardY + csz + 24.f;

            ImGui::SetCursorPos({padX, btnY});
            ImVec2 tl = ImGui::GetCursorScreenPos();
            ImVec2 br = {tl.x + btnW, tl.y + btnH};

            bool hov = ImGui::IsMouseHoveringRect(tl, br);
            bool clk = hov && ImGui::IsMouseClicked(0);

            // Red-tinted button
            ImU32 bgCol = hov ? IM_COL32(60, 14, 14, 240) : IM_COL32(30, 10, 10, 220);
            ImU32 brCol = hov ? IM_COL32(200, 50, 50, 220) : IM_COL32(100, 30, 30, 160);
            cdl->AddRectFilled(tl, br, bgCol, 10.f);
            cdl->AddRect      (tl, br, brCol, 10.f, 0, 1.2f);

            // Power-off icon
            float icx = tl.x + 22.f;
            float icy = tl.y + btnH * 0.5f;
            ImU32 ic  = hov ? IM_COL32(255, 80, 80, 255) : IM_COL32(200, 70, 70, 220);
            cdl->AddCircle      ({icx, icy}, 8.f,  ic, 20, 1.8f);
            cdl->AddLine        ({icx, icy - 10.f}, {icx, icy - 4.f}, ic, 2.2f);
            // Gap in the circle (white out a small arc at top)
            cdl->AddRectFilled  ({icx - 3.f, icy - 11.f}, {icx + 3.f, icy - 7.f},
                IM_COL32(30, 10, 10, 255));

            // Label
            const char* signoutLbl = "Sign Out";
            float lw = ImGui::CalcTextSize(signoutLbl).x;
            float lx = icx + 18.f;
            float ly = icy - ImGui::GetTextLineHeight() * 0.5f - tl.y;
            ImGui::SetCursorPos({padX + 36.f, btnY + ly});
            ImGui::TextColored(
                hov ? ImVec4(1.f, 0.5f, 0.5f, 1.f) : ImVec4(0.75f, 0.35f, 0.35f, 1.f),
                "%s", signoutLbl);

            if (clk) ClearSession();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Auth screen (login / signup) ──────────────────────────────────────────────

void CoreGui::DrawAuthScreen() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;
    const bool isSignup = (m_authState == AuthState::Signup);

    // ── Poll async auth result ────────────────────────────────────────────────
    if (m_authInFlight && m_authFuture.valid()) {
        auto status = m_authFuture.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready) {
            AuthResult res = m_authFuture.get();
            m_authInFlight = false;
            if (res.ok) {
                m_loggedIn = true;
                m_username = m_pendingUser;
                SaveSession();   // persist so next launch auto-logs in
                memset(m_inputUser,        0, sizeof(m_inputUser));
                memset(m_inputPass,        0, sizeof(m_inputPass));
                memset(m_inputPassConfirm, 0, sizeof(m_inputPassConfirm));
                m_authError[0] = '\0';
            } else {
                snprintf(m_authError, sizeof(m_authError), "%s", res.error.c_str());
            }
        }
    }
    if (m_loggedIn) return;  // home screen takes over next frame

    // ── Full-screen background ────────────────────────────────────────────────
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize({W, H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.07f, 0.07f, 0.10f, 1.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::Begin("##auth_bg", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // ── Card window ───────────────────────────────────────────────────────────
    const float cw   = 400.f;
    const float padX = 36.f, padY = 32.f;
    float baseH = isSignup ? 518.f : 444.f;
    if (m_authError[0] && strchr(m_authError, '\n')) baseH += 20.f;
    const float ch = baseH;

    ImGui::SetNextWindowPos({(W - cw) * 0.5f, (H - ch) * 0.5f});
    ImGui::SetNextWindowSize({cw, ch});

    ImGui::PushStyleColor(ImGuiCol_WindowBg,       {0.044f, 0.044f, 0.068f, 0.99f});
    ImGui::PushStyleColor(ImGuiCol_Border,         {0.14f,  0.14f,  0.22f,  1.f  });
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        {0.09f,  0.09f,  0.14f,  1.f  });
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, {0.12f,  0.12f,  0.18f,  1.f  });
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  {0.07f,  0.07f,  0.11f,  1.f  });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  14.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   {padX, padY});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,    8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {14.f, 10.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     {0.f, 10.f});

    ImGui::Begin("##auth_card", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoSavedSettings);

    float innerW = ImGui::GetContentRegionAvail().x;

    // ── C2P logo pill ─────────────────────────────────────────────────────────
    {
        const char* mono = "C2P";
        ImVec2 tsz = ImGui::CalcTextSize(mono);
        float lx = (innerW - tsz.x - 14.f) * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cur = ImGui::GetCursorScreenPos(); cur.x += lx;
        dl->AddRectFilled({cur.x - 7.f, cur.y - 4.f},
                          {cur.x + tsz.x + 7.f, cur.y + tsz.y + 4.f},
                          IM_COL32(28, 92, 240, 210), 7.f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lx);
        ImGui::TextColored({1.f, 1.f, 1.f, 1.f}, "%s", mono);
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

    // ── Title + subtitle ──────────────────────────────────────────────────────
    ImGui::PushFont(m_fontTitle);
    {
        const char* t = isSignup ? "Create account" : "Welcome back";
        ImGui::SetCursorPosX((innerW - ImGui::CalcTextSize(t).x) * 0.5f);
        ImGui::TextColored({1.f, 1.f, 1.f, 0.95f}, "%s", t);
    }
    ImGui::PopFont();
    {
        const char* s = isSignup ? "Join CreateToPlay" : "Sign in to CreateToPlay";
        ImGui::SetCursorPosX((innerW - ImGui::CalcTextSize(s).x) * 0.5f);
        ImGui::TextColored({0.46f, 0.46f, 0.58f, 1.f}, "%s", s);
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.f);

    // ── Form fields (disabled while request in flight) ────────────────────────
    const bool busy = m_authInFlight;
    if (busy) ImGui::BeginDisabled();

    ImGui::TextColored({0.60f, 0.60f, 0.72f, 1.f}, "Username");
    ImGui::SetNextItemWidth(innerW);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool enterUser = ImGui::InputText("##authuser", m_inputUser, sizeof(m_inputUser),
        ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
    ImGui::TextColored({0.60f, 0.60f, 0.72f, 1.f}, "Password");
    ImGui::SetNextItemWidth(innerW);
    bool enterPass = ImGui::InputText("##authpass", m_inputPass, sizeof(m_inputPass),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);

    bool enterConfirm = false;
    if (isSignup) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
        ImGui::TextColored({0.60f, 0.60f, 0.72f, 1.f}, "Confirm password");
        ImGui::SetNextItemWidth(innerW);
        enterConfirm = ImGui::InputText("##authconfirm", m_inputPassConfirm,
            sizeof(m_inputPassConfirm),
            ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    }

    if (busy) ImGui::EndDisabled();

    // ── Error / connecting feedback ───────────────────────────────────────────
    if (busy) {
        // Animated dots: "Connecting." / ".." / "..."
        static const char* kDots[] = {"Connecting.  ", "Connecting.. ", "Connecting..."};
        int tick = (int)(ImGui::GetTime() * 2.0) % 3;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
        float tw = ImGui::CalcTextSize(kDots[tick]).x;
        ImGui::SetCursorPosX((innerW - tw) * 0.5f);
        ImGui::TextColored({0.50f, 0.70f, 1.f, 1.f}, "%s", kDots[tick]);
    } else if (m_authError[0]) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerW);
        ImGui::TextColored({0.95f, 0.35f, 0.35f, 1.f}, "%s", m_authError);
        ImGui::PopTextWrapPos();
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.f);

    // ── Primary button ────────────────────────────────────────────────────────
    bool doSubmit = !busy && (enterUser || enterPass || enterConfirm);

    if (busy) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.11f, 0.36f, 0.94f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f, 0.48f, 1.00f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.07f, 0.26f, 0.80f, 1.f});
    if (ImGui::Button(isSignup ? "Create Account" : "Log In", {innerW, 46.f}))
        doSubmit = true;
    ImGui::PopStyleColor(3);
    if (busy) ImGui::EndDisabled();

    // ── Submit: launch async auth ─────────────────────────────────────────────
    if (doSubmit && !busy) {
        m_authError[0] = '\0';
        if (isSignup && strcmp(m_inputPass, m_inputPassConfirm) != 0) {
            snprintf(m_authError, sizeof(m_authError), "Passwords do not match");
        } else {
            m_pendingUser  = m_inputUser;
            m_authInFlight = true;

            // Capture by value so the lambda is self-contained in its thread
            std::string host = m_authHost;
            uint16_t    port = m_authPort;
            std::string user = m_inputUser;
            std::string pass = m_inputPass;
            bool        reg  = isSignup;

            m_authFuture = std::async(std::launch::async,
                [host, port, user, pass, reg]() -> AuthResult {
                    AuthClient ac;
                    return reg ? ac.Register(host, port, user, pass)
                               : ac.Login   (host, port, user, pass);
                });
        }
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

    // ── Divider ───────────────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Separator, {0.18f, 0.18f, 0.28f, 1.f});
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);

    // ── Switch-mode link + Guest shortcut ─────────────────────────────────────
    auto LinkButton = [&](const char* lbl) -> bool {
        float lw = ImGui::CalcTextSize(lbl).x;
        ImGui::SetCursorPosX((innerW - lw) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.f,0.f,0.f,0.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.f,0.f,0.f,0.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.f,0.f,0.f,0.f});
        ImGui::PushStyleColor(ImGuiCol_Text,          {0.44f,0.64f,1.f,1.f});
        bool clicked = ImGui::Button(lbl);
        ImGui::PopStyleColor(4);
        return clicked;
    };

    const char* switchLbl = isSignup
        ? "Already have an account?  Log in"
        : "Don't have an account?  Sign up";
    if (LinkButton(switchLbl)) {
        m_authState    = isSignup ? AuthState::Login : AuthState::Signup;
        m_authError[0] = '\0';
    }

    // Guest option — skips server auth entirely
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f);
    if (LinkButton("Continue as Guest")) {
        char guestName[24];
        snprintf(guestName, sizeof(guestName), "Guest_%u",
            (unsigned)(SDL_GetTicks() % 9999));
        m_loggedIn = true;
        m_username  = guestName;
        SaveSession();
    }

    ImGui::End();
    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(5);
}
