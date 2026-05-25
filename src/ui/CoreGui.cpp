#include "CoreGui.h"
#include "embedded/EmbeddedFont.h"
#include "embedded/EmbeddedAssets.h"
#include "auth/FriendClient.h"
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
            LoadAppSettings();   // load app preferences
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
            LoadProfile();
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

void CoreGui::SaveAppSettings() {
    if (m_sessionPath.empty()) return;   // re-use same pref dir
    std::string path = m_sessionPath.substr(0, m_sessionPath.rfind('/') + 1) + "settings.dat";
    if (FILE* f = fopen(path.c_str(), "w")) {
        fprintf(f, "accentR=%f\naccentG=%f\naccentB=%f\n",
                m_appSettings.accentR, m_appSettings.accentG, m_appSettings.accentB);
        fprintf(f, "uiScale=%f\n",    m_appSettings.uiScale);
        fprintf(f, "nametags=%d\n",   m_appSettings.showNametags    ? 1 : 0);
        fprintf(f, "online=%d\n",     m_appSettings.showOnlineStatus ? 1 : 0);
        fclose(f);
    }
}

void CoreGui::LoadAppSettings() {
    if (m_sessionPath.empty()) return;
    std::string path = m_sessionPath.substr(0, m_sessionPath.rfind('/') + 1) + "settings.dat";
    if (FILE* f = fopen(path.c_str(), "r")) {
        char key[32]; float fv; int iv;
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if      (sscanf(line, "accentR=%f",  &fv) == 1) m_appSettings.accentR = fv;
            else if (sscanf(line, "accentG=%f",  &fv) == 1) m_appSettings.accentG = fv;
            else if (sscanf(line, "accentB=%f",  &fv) == 1) m_appSettings.accentB = fv;
            else if (sscanf(line, "uiScale=%f",  &fv) == 1) m_appSettings.uiScale = fv;
            else if (sscanf(line, "nametags=%d", &iv) == 1) m_appSettings.showNametags     = iv != 0;
            else if (sscanf(line, "online=%d",   &iv) == 1) m_appSettings.showOnlineStatus = iv != 0;
        }
        fclose(f);
    }
}

void CoreGui::SaveProfile() {
    if (m_sessionPath.empty()) return;
    std::string path = m_sessionPath.substr(0, m_sessionPath.rfind('/') + 1) + "profile.dat";
    if (FILE* f = fopen(path.c_str(), "w")) {
        fprintf(f, "displayName=%s\n", m_displayName.c_str());
        fprintf(f, "email=%s\n",       m_email.c_str());
        fprintf(f, "phone=%s\n",       m_phoneNumber.c_str());
        fclose(f);
    }
}

void CoreGui::LoadProfile() {
    if (m_sessionPath.empty()) return;
    std::string path = m_sessionPath.substr(0, m_sessionPath.rfind('/') + 1) + "profile.dat";
    if (FILE* f = fopen(path.c_str(), "r")) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            // Strip trailing CR/LF
            for (char* p = line; *p; ++p)
                if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
            if      (strncmp(line, "displayName=", 12) == 0) m_displayName = line + 12;
            else if (strncmp(line, "email=",        6) == 0) m_email       = line + 6;
            else if (strncmp(line, "phone=",        6) == 0) m_phoneNumber = line + 6;
        }
        fclose(f);
    }
}

// ── Friend system helpers ─────────────────────────────────────────────────────

void CoreGui::KickFriendRefresh() {
    if (!m_loggedIn || m_authHost.empty()) return;
    m_friendRefreshT = 15.f;   // next refresh in 15 s

    if (!m_friendListInFlight) {
        m_friendListInFlight = true;
        std::string host = m_authHost; uint16_t port = m_authPort;
        std::string user = m_username;
        m_friendListFuture = std::async(std::launch::async,
            [host, port, user]() -> FriendListResult {
                return FriendClient().GetFriends(host, port, user);
            });
    }
    if (!m_friendReqsInFlight) {
        m_friendReqsInFlight = true;
        std::string host = m_authHost; uint16_t port = m_authPort;
        std::string user = m_username;
        m_friendReqsFuture = std::async(std::launch::async,
            [host, port, user]() -> FriendReqsResult {
                return FriendClient().GetRequests(host, port, user);
            });
    }
}

void CoreGui::PollFriendFutures() {
    // Friend list
    if (m_friendListInFlight && m_friendListFuture.valid()) {
        if (m_friendListFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto res = m_friendListFuture.get();
            m_friendListInFlight = false;
            if (res.ok) m_friends = std::move(res.friends);
        }
    }
    // Friend requests
    if (m_friendReqsInFlight && m_friendReqsFuture.valid()) {
        if (m_friendReqsFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto res = m_friendReqsFuture.get();
            m_friendReqsInFlight = false;
            if (res.ok) m_friendRequests = std::move(res.requests);
        }
    }
    // Generic op (add/accept/decline/remove)
    if (m_friendOpInFlight && m_friendOpFuture.valid()) {
        if (m_friendOpFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto res = m_friendOpFuture.get();
            m_friendOpInFlight = false;
            if (!res.ok) m_addFriendStatus = "Error: " + res.error;
            else         m_addFriendStatus = "Sent!";
        }
    }
    // Join friend
    if (m_joinInFlight && m_joinFriendFuture.valid()) {
        if (m_joinFriendFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto res = m_joinFriendFuture.get();
            m_joinInFlight = false;
            if (res.ok && !res.serverAddr.empty()) {
                m_hasOverrideJoinAddr = true;
                m_overrideJoinAddr    = res.serverAddr;
                m_gameStarted         = true;   // trigger Engine game-start path
            } else {
                m_addFriendStatus = "Join failed: " + res.error;
            }
        }
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
    if (m_appSettings.showNametags && !m_nametags.empty()) {
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

    // ── Shift-lock crosshair dot ──────────────────────────────────────────────
    if (m_shiftLock && !m_menuOpen) {
        ImDrawList* dl     = ImGui::GetForegroundDrawList();
        ImVec2      center = ImGui::GetMainViewport()->GetCenter();
        // Outer dark ring for contrast on any background
        dl->AddCircle(center, 5.5f, IM_COL32(0, 0, 0, 160), 16, 1.8f);
        // White filled dot
        dl->AddCircleFilled(center, 3.0f, IM_COL32(255, 255, 255, 210), 16);
    }

    DrawMenuButton(); // always on top

    // ── Connection status pill (top-right) ────────────────────────────────────
    if (m_appSettings.showOnlineStatus) {
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
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.f, 1.f, 1.f, 0.60f));
    ImGui::Separator();
    ImGui::PopStyleColor();

    // ── Player list ───────────────────────────────────────────────────────────
    {
        float listTopY  = ImGui::GetCursorPosY() + 10.f;
        float listBotY  = ph - kBotH - 10.f;
        float listH     = listBotY - listTopY;
        float listW     = pw - kPad * 2.f;

        // Header
        ImGui::SetCursorPosY(listTopY);
        ImGui::SetCursorPosX(kPad);
        char hdr[40];
        snprintf(hdr, sizeof(hdr), "Players  (%d)", (int)m_sessionPlayers.size());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.57f, 0.68f, 1.f));
        ImGui::TextUnformatted(hdr);
        ImGui::PopStyleColor();

        float rowsTopY = ImGui::GetCursorPosY() + 6.f;
        float rowsH    = listBotY - rowsTopY - 4.f;

        ImGui::SetCursorPos({kPad, rowsTopY});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, 2.f});
        ImGui::BeginChild("##playerList", {listW, rowsH}, false);

        ImDrawList* pdl = ImGui::GetWindowDrawList();
        const float rowH    = 34.f;
        const float dotR    = 5.f;
        const float addBtnSz = 26.f;

        for (int pi = 0; pi < (int)m_sessionPlayers.size(); ++pi) {
            const std::string& pname = m_sessionPlayers[pi];
            bool isLocal  = (pname == m_username);
            bool alreadySent = m_sentFriendReqs.count(pname) > 0;

            ImVec2 rowTL = ImGui::GetCursorScreenPos();
            ImVec2 rowBR = {rowTL.x + listW, rowTL.y + rowH};

            // Hover tint
            bool rowHov = ImGui::IsMouseHoveringRect(rowTL, rowBR);
            if (rowHov)
                pdl->AddRectFilled(rowTL, rowBR, IM_COL32(255, 255, 255, 6), 6.f);

            // Online dot
            pdl->AddCircleFilled({rowTL.x + dotR + 2.f, rowTL.y + rowH * 0.5f},
                                  dotR, IM_COL32(60, 220, 80, 255));

            // Name
            float nameX = rowTL.x + dotR * 2.f + 10.f;
            float nameY = rowTL.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            ImU32 nameCol = isLocal ? IM_COL32(180, 210, 255, 255)
                                    : IM_COL32(220, 220, 235, 240);
            pdl->AddText({nameX, nameY}, nameCol, pname.c_str());
            if (isLocal) {
                float youX = nameX + ImGui::CalcTextSize(pname.c_str()).x + 8.f;
                pdl->AddText({youX, nameY}, IM_COL32(100, 120, 170, 200), "(you)");
            }

            // "+" add friend button — only for others, icon only
            if (!isLocal) {
                float bx = rowTL.x + listW - addBtnSz - 4.f;
                float by = rowTL.y + (rowH - addBtnSz) * 0.5f;
                ImVec2 bTL = {bx, by};
                ImVec2 bBR = {bx + addBtnSz, by + addBtnSz};
                bool bHov = ImGui::IsMouseHoveringRect(bTL, bBR);
                bool bClk = bHov && ImGui::IsMouseClicked(0);

                if (alreadySent) {
                    // Checkmark circle
                    pdl->AddCircleFilled({bx + addBtnSz*0.5f, by + addBtnSz*0.5f},
                                         addBtnSz*0.5f, IM_COL32(20, 130, 40, 180), 16);
                    // ✓ tick
                    float cx = bx + addBtnSz*0.5f, cy = by + addBtnSz*0.5f;
                    pdl->AddLine({cx - 5.f, cy}, {cx - 1.f, cy + 4.f}, IM_COL32(255,255,255,230), 2.f);
                    pdl->AddLine({cx - 1.f, cy + 4.f}, {cx + 5.f, cy - 4.f}, IM_COL32(255,255,255,230), 2.f);
                } else {
                    // "+" circle button
                    ImU32 bgC = bHov ? IM_COL32(28, 92, 240, 200) : IM_COL32(40, 40, 65, 180);
                    pdl->AddCircleFilled({bx + addBtnSz*0.5f, by + addBtnSz*0.5f},
                                         addBtnSz*0.5f, bgC, 16);
                    float cx = bx + addBtnSz*0.5f, cy = by + addBtnSz*0.5f, arm = 5.f;
                    pdl->AddLine({cx - arm, cy}, {cx + arm, cy}, IM_COL32(255,255,255,240), 2.f);
                    pdl->AddLine({cx, cy - arm}, {cx, cy + arm}, IM_COL32(255,255,255,240), 2.f);

                    if (bClk && !m_friendOpInFlight) {
                        m_sentFriendReqs.insert(pname);
                        m_friendOpInFlight = true;
                        std::string host = m_authHost; uint16_t port = m_authPort;
                        std::string from = m_username, to = pname;
                        m_friendOpFuture = std::async(std::launch::async,
                            [host, port, from, to]() -> FriendOpResult {
                                return FriendClient().SendRequest(host, port, from, to);
                            });
                    }
                }

                // Invisible button for ImGui focus bookkeeping
                ImGui::SetCursorScreenPos(bTL);
                char bid[32]; snprintf(bid, sizeof(bid), "##padd%d", pi);
                ImGui::InvisibleButton(bid, {addBtnSz, addBtnSz});
            }

            // Advance row
            ImGui::SetCursorScreenPos({rowTL.x, rowTL.y + rowH + 2.f});
            ImGui::Dummy({listW, 0.f});
        }

        if (m_sessionPlayers.empty()) {
            ImGui::SetCursorPosY(rowsH * 0.3f);
            ImVec2 ts = ImGui::CalcTextSize("No players connected");
            ImGui::SetCursorPosX((listW - ts.x) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.45f, 1.f));
            ImGui::TextUnformatted("No players connected");
            ImGui::PopStyleColor();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

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

    // Settings is full-screen — skip the home page entirely while it's open
    if (m_settingsOpen) {
        DrawSettingsPanel();
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

    // Poll friend async futures on every home-page frame
    PollFriendFutures();

    // Auto-refresh friends list (timer counts down, refreshes when 0)
    {
        float dt = ImGui::GetIO().DeltaTime;
        m_friendRefreshT -= dt;
        if (m_friendRefreshT <= 0.f) KickFriendRefresh();
    }

    struct SideItem { const char* label; };
    static const SideItem kItems[] = { {"Home"}, {"Avatar"}, {"Friends"}, {"More"} };

    // Push nav items down below the logo
    const float navOffsetY = 58.f;

    for (int i = 0; i < 4; ++i) {
        float by = navOffsetY + i * (btnSlotH + 4.f);

        ImGui::SetCursorPos({0.f, by});
        char bid[16]; snprintf(bid, sizeof(bid), "##sb%d", i);
        bool clicked = ImGui::InvisibleButton(bid, {sideW, btnSlotH});
        bool hovered = ImGui::IsItemHovered();
        if (clicked) m_sidebarTab = i;
        bool active  = (m_sidebarTab == i);

        if (active)
            dl->AddRectFilled({0.f, by}, {sideW, by + btnSlotH}, IM_COL32(28, 92, 240, 28));
        else if (hovered)
            dl->AddRectFilled({0.f, by}, {sideW, by + btnSlotH}, IM_COL32(255, 255, 255, 12));
        if (active)
            dl->AddRectFilled({0.f, by}, {3.f, by + btnSlotH}, IM_COL32(28, 92, 240, 255), 2.f);

        ImU32 iconCol = active ? IM_COL32(80, 150, 255, 255) : IM_COL32(170, 170, 185, 255);
        float cx = sideW * 0.5f;
        float iy = by + 12.f;

        if (i == 0) {
            // House
            dl->AddTriangleFilled({cx, iy}, {cx - 12.f, iy + 11.f}, {cx + 12.f, iy + 11.f}, iconCol);
            dl->AddRectFilled({cx - 8.f, iy + 11.f}, {cx + 8.f, iy + 24.f}, iconCol, 1.f);
            dl->AddRectFilled({cx - 3.5f, iy + 16.f}, {cx + 3.5f, iy + 24.f}, IM_COL32(8, 8, 14, 255));
        } else if (i == 1) {
            // Person (avatar)
            dl->AddCircleFilled({cx, iy + 6.f}, 5.5f, iconCol);
            dl->AddRectFilled({cx - 7.f, iy + 13.f}, {cx + 7.f, iy + 24.f}, iconCol, 3.f);
        } else if (i == 2) {
            // Friends — two people + small "+" badge
            // Back person (offset left)
            dl->AddCircleFilled({cx - 6.f, iy + 6.f}, 4.5f, iconCol);
            dl->AddRectFilled({cx - 13.f, iy + 12.f}, {cx + 1.f, iy + 22.f}, iconCol, 2.f);
            // Front person
            dl->AddCircleFilled({cx + 3.f, iy + 6.f}, 5.f, iconCol);
            dl->AddRectFilled({cx - 4.f, iy + 13.f}, {cx + 10.f, iy + 24.f}, iconCol, 2.f);
            // "+" badge (top-right of icon area)
            float bx = cx + 9.f, by2 = iy - 1.f, arm = 3.5f;
            dl->AddCircleFilled({bx, by2}, arm + 2.f, IM_COL32(28, 92, 240, 220), 12);
            dl->AddLine({bx - arm, by2}, {bx + arm, by2}, IM_COL32(255,255,255,255), 1.5f);
            dl->AddLine({bx, by2 - arm}, {bx, by2 + arm}, IM_COL32(255,255,255,255), 1.5f);
            // Pending request badge
            if (!m_friendRequests.empty()) {
                float bdgX = cx + 14.f, bdgY = iy + 18.f;
                dl->AddCircleFilled({bdgX, bdgY}, 6.f, IM_COL32(220, 45, 45, 240), 12);
                char nc[4]; snprintf(nc, sizeof(nc), "%d", (int)m_friendRequests.size());
                ImVec2 ntsz = ImGui::CalcTextSize(nc);
                dl->AddText({bdgX - ntsz.x*0.5f, bdgY - ntsz.y*0.5f},
                             IM_COL32(255,255,255,255), nc);
            }
        } else {
            // More — three dots
            for (int d = 0; d < 3; ++d)
                dl->AddCircleFilled({cx - 8.f + d * 8.f, iy + 12.f}, 2.8f, iconCol);
        }

        // Label
        float lw = ImGui::CalcTextSize(kItems[i].label).x;
        ImGui::SetCursorPos({(sideW - lw) * 0.5f, by + 40.f});
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

    } else if (m_sidebarTab == 2) {
        DrawFriendsTab(cdl, padX, W, sideW);
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
            bool clk = hov && ImGui::IsMouseClicked(0);

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
                if (clk) m_settingsOpen = true;
            } else {
                cdl->AddCircleFilled({cx, icy - 8.f}, 9.f, ic);
                cdl->AddRectFilled({cx - 11.f, icy + 3.f}, {cx + 11.f, icy + 18.f}, ic, 5.f);
                // Profile card — placeholder, no action yet
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

// ── Friends tab (home page) ───────────────────────────────────────────────────

void CoreGui::DrawFriendsTab(ImDrawList* cdl, float padX, float W, float sideW)
{
    // Usable width inside the content child, with equal left/right padding
    const float rowCW = (W - sideW) - padX * 2.f;

    // Record default line height BEFORE pushing the title font
    const float bodyLH = ImGui::GetTextLineHeight();

    // Title
    ImGui::SetCursorPos({padX, 30.f});
    ImGui::PushFont(m_fontTitle);
    ImGui::TextColored({0.88f, 0.88f, 0.94f, 1.f}, "Friends");
    ImGui::PopFont();

    float curY = 30.f + bodyLH + 22.f;

    // ── Add Friend row ────────────────────────────────────────────────────────
    {
        const float sendW = 76.f, gap = 8.f;
        const float inputW = rowCW - sendW - gap;

        ImGui::SetCursorPos({padX, curY});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        {0.09f, 0.09f, 0.14f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, {0.12f, 0.12f, 0.18f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  {0.07f, 0.07f, 0.11f, 1.f});
        ImGui::SetNextItemWidth(inputW);
        bool enter = ImGui::InputText("##addfriend", m_addFriendBuf, sizeof(m_addFriendBuf),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, gap);
        if (m_friendOpInFlight) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.11f, 0.36f, 0.94f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f, 0.48f, 1.00f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.07f, 0.26f, 0.80f, 1.f});
        bool sendClk = ImGui::Button("Send##addBtn", {sendW, 0.f});
        ImGui::PopStyleColor(3);
        if (m_friendOpInFlight) ImGui::EndDisabled();
        ImGui::PopStyleVar();   // FrameRounding

        if ((sendClk || enter) && m_addFriendBuf[0] && !m_friendOpInFlight) {
            std::string target(m_addFriendBuf);
            m_addFriendStatus.clear();
            m_friendOpInFlight = true;
            std::string host = m_authHost; uint16_t port = m_authPort;
            std::string user = m_username;
            m_friendOpFuture = std::async(std::launch::async,
                [host, port, user, target]() -> FriendOpResult {
                    return FriendClient().SendRequest(host, port, user, target);
                });
            memset(m_addFriendBuf, 0, sizeof(m_addFriendBuf));
        }

        curY += ImGui::GetFrameHeight() + 6.f;

        if (!m_addFriendStatus.empty()) {
            bool isErr = m_addFriendStatus.rfind("Error", 0) == 0;
            ImGui::SetCursorPos({padX, curY});
            ImGui::TextColored(
                isErr ? ImVec4(0.95f, 0.35f, 0.35f, 1.f) : ImVec4(0.45f, 0.80f, 0.55f, 1.f),
                "%s", m_addFriendStatus.c_str());
            curY += bodyLH + 4.f;
        }
    }

    curY += 12.f;

    // ── Pending Requests ──────────────────────────────────────────────────────
    if (!m_friendRequests.empty()) {
        ImGui::SetCursorPos({padX, curY});
        char rhdr[48]; snprintf(rhdr, sizeof(rhdr), "Requests  (%d)", (int)m_friendRequests.size());
        ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.57f, 0.65f, 1.f});
        ImGui::TextUnformatted(rhdr);
        ImGui::PopStyleColor();
        curY += bodyLH + 8.f;

        const float rRowH = 40.f, rBtnW = 68.f, rBtnH = 26.f;
        int  eraseIdx = -1;
        bool doAccept = false;

        for (int i = 0; i < (int)m_friendRequests.size(); ++i) {
            const std::string& from = m_friendRequests[i].fromUser;

            ImGui::SetCursorPos({padX, curY});
            ImVec2 rowTL = ImGui::GetCursorScreenPos();

            if (ImGui::IsMouseHoveringRect(rowTL, {rowTL.x + rowCW, rowTL.y + rRowH}))
                cdl->AddRectFilled(rowTL, {rowTL.x + rowCW, rowTL.y + rRowH},
                                   IM_COL32(255, 255, 255, 5), 6.f);

            // Person icon
            float icy = rowTL.y + rRowH * 0.5f;
            float icx = rowTL.x + 14.f;
            cdl->AddCircleFilled({icx, icy - 5.f}, 5.f,  IM_COL32(100, 130, 200, 220));
            cdl->AddRectFilled({icx - 6.f, icy + 1.f}, {icx + 6.f, icy + 11.f},
                               IM_COL32(100, 130, 200, 220), 3.f);

            // Name
            cdl->AddText({rowTL.x + 30.f, rowTL.y + (rRowH - bodyLH) * 0.5f},
                         IM_COL32(210, 210, 230, 240), from.c_str());

            // Buttons — positioned from right edge
            float rightEdge = rowTL.x + rowCW;
            float decX = rightEdge - rBtnW;
            float accX = decX - rBtnW - 8.f;
            float btnY = rowTL.y + (rRowH - rBtnH) * 0.5f;

            ImGui::SetCursorScreenPos({accX, btnY});
            ImGui::PushStyleColor(ImGuiCol_Button,        {0.11f, 0.36f, 0.94f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f, 0.48f, 1.00f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.07f, 0.26f, 0.80f, 1.f});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.f);
            char aid[32]; snprintf(aid, sizeof(aid), "Accept##ra%d", i);
            if (ImGui::Button(aid, {rBtnW, rBtnH})) { eraseIdx = i; doAccept = true; }
            ImGui::PopStyleVar(); ImGui::PopStyleColor(3);

            ImGui::SetCursorScreenPos({decX, btnY});
            ImGui::PushStyleColor(ImGuiCol_Button,        {0.14f, 0.08f, 0.08f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.10f, 0.10f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f, 0.05f, 0.05f, 1.f});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.f);
            char did[32]; snprintf(did, sizeof(did), "Decline##rd%d", i);
            if (ImGui::Button(did, {rBtnW, rBtnH})) { eraseIdx = i; doAccept = false; }
            ImGui::PopStyleVar(); ImGui::PopStyleColor(3);

            curY += rRowH + 4.f;
        }

        // Handle accept / decline after the loop to avoid mutating the vector mid-loop
        if (eraseIdx >= 0 && !m_friendOpInFlight) {
            std::string fromU = m_friendRequests[eraseIdx].fromUser;
            m_friendOpInFlight = true;
            std::string host = m_authHost; uint16_t port = m_authPort;
            std::string user = m_username;
            bool accept = doAccept;
            m_friendOpFuture = std::async(std::launch::async,
                [host, port, user, fromU, accept]() -> FriendOpResult {
                    return accept
                        ? FriendClient().AcceptRequest (host, port, user, fromU)
                        : FriendClient().DeclineRequest(host, port, user, fromU);
                });
            m_friendRequests.erase(m_friendRequests.begin() + eraseIdx);
            if (accept) KickFriendRefresh();  // refresh friends list after accepting
        }

        // Thin divider between requests and friends list
        curY += 8.f;
        ImGui::SetCursorPos({padX, curY});
        ImVec2 sp = ImGui::GetCursorScreenPos();
        cdl->AddLine(sp, {sp.x + rowCW, sp.y}, IM_COL32(35, 35, 55, 200), 1.f);
        curY += 12.f;
    }

    // ── Friends list ──────────────────────────────────────────────────────────
    {
        ImGui::SetCursorPos({padX, curY});
        char fhdr[48]; snprintf(fhdr, sizeof(fhdr), "Friends  (%d)", (int)m_friends.size());
        ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.57f, 0.65f, 1.f});
        ImGui::TextUnformatted(fhdr);
        ImGui::PopStyleColor();
        curY += bodyLH + 8.f;

        if (m_friends.empty()) {
            ImGui::SetCursorPos({padX, curY + 18.f});
            const char* emptyMsg = m_friendListInFlight
                ? "Refreshing friends list…"
                : "No friends yet — add someone above!";
            float tw = ImGui::CalcTextSize(emptyMsg).x;
            ImGui::SetCursorPosX(padX + (rowCW - tw) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, {0.32f, 0.32f, 0.42f, 1.f});
            ImGui::TextUnformatted(emptyMsg);
            ImGui::PopStyleColor();
        } else {
            // Scrollable friends list — fills remaining vertical space
            ImGui::SetCursorPos({padX, curY});
            float listH = ImGui::GetContentRegionAvail().y - 8.f;
            if (listH < 40.f) listH = 40.f;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.f, 0.f, 0.f, 0.f});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, 2.f});
            ImGui::BeginChild("##friendList", {rowCW, listH}, false);
            ImDrawList* fdl = ImGui::GetWindowDrawList();

            const float fRowH = 44.f;
            const float joinW = 54.f, joinH = 28.f;
            const float dotR  = 5.f;

            // Sort: online/in-game first, then plain online, then offline
            std::vector<int> sorted;
            sorted.reserve(m_friends.size());
            for (int i = 0; i < (int)m_friends.size(); ++i) sorted.push_back(i);
            std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
                int sa = m_friends[a].inGame ? 2 : (m_friends[a].online ? 1 : 0);
                int sb = m_friends[b].inGame ? 2 : (m_friends[b].online ? 1 : 0);
                return sa > sb;
            });

            for (int idx : sorted) {
                const FriendEntry& fr = m_friends[idx];

                ImVec2 rowTL = ImGui::GetCursorScreenPos();
                ImVec2 rowBR = {rowTL.x + rowCW, rowTL.y + fRowH};

                bool rowHov = ImGui::IsMouseHoveringRect(rowTL, rowBR);
                if (rowHov)
                    fdl->AddRectFilled(rowTL, rowBR, IM_COL32(255, 255, 255, 7), 6.f);

                // Status dot
                float dotCX = rowTL.x + 12.f;
                float dotCY = rowTL.y + fRowH * 0.5f;
                ImU32 dotCol = fr.inGame  ? IM_COL32(80, 200, 255, 255) :
                               fr.online  ? IM_COL32(60, 220, 80,  255) :
                                            IM_COL32(80, 80,  90,  200);
                fdl->AddCircleFilled({dotCX, dotCY}, dotR, dotCol);

                // Name
                float nameX = rowTL.x + 26.f;
                float nameY = rowTL.y + (fr.online ? (fRowH * 0.5f - bodyLH - 1.f)
                                                   : (fRowH - bodyLH) * 0.5f);
                ImU32 nameCol = fr.online ? IM_COL32(220, 220, 235, 240)
                                          : IM_COL32(110, 110, 125, 190);
                fdl->AddText({nameX, nameY}, nameCol, fr.username.c_str());

                // Status sub-label (only when online)
                if (fr.online) {
                    const char* sub = fr.inGame ? "In Game" : "Online";
                    ImU32 subCol = fr.inGame ? IM_COL32(80, 200, 255, 180)
                                             : IM_COL32(60, 200, 80,  180);
                    fdl->AddText({nameX, rowTL.y + fRowH * 0.5f + 2.f}, subCol, sub);
                }

                // Join button — only for in-game friends and when no join is in flight
                if (fr.inGame && !m_joinInFlight) {
                    float jx = rowTL.x + rowCW - joinW - 8.f;
                    float jy = rowTL.y + (fRowH - joinH) * 0.5f;
                    ImVec2 jTL = {jx, jy}, jBR = {jx + joinW, jy + joinH};

                    bool jHov = ImGui::IsMouseHoveringRect(jTL, jBR);
                    fdl->AddRectFilled(jTL, jBR,
                        jHov ? IM_COL32(40, 110, 255, 230) : IM_COL32(28, 92, 240, 190), 6.f);
                    fdl->AddRect(jTL, jBR, IM_COL32(60, 130, 255, 140), 6.f, 0, 1.f);

                    ImVec2 jts = ImGui::CalcTextSize("Join");
                    fdl->AddText({jx + (joinW - jts.x) * 0.5f,
                                  jy + (joinH - jts.y) * 0.5f},
                                 IM_COL32(255, 255, 255, 240), "Join");

                    ImGui::SetCursorScreenPos(jTL);
                    char jid[32]; snprintf(jid, sizeof(jid), "##join%d", idx);
                    if (ImGui::InvisibleButton(jid, {joinW, joinH})) {
                        m_joinInFlight = true;
                        std::string host = m_authHost; uint16_t port = m_authPort;
                        std::string user = m_username, target = fr.username;
                        m_joinFriendFuture = std::async(std::launch::async,
                            [host, port, user, target]() -> JoinFriendResult {
                                return FriendClient().JoinFriend(host, port, user, target);
                            });
                    }
                }

                // Advance cursor to next row
                ImGui::SetCursorScreenPos({rowTL.x, rowTL.y + fRowH + 2.f});
                ImGui::Dummy({rowCW, 0.f});
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
    }
}

// ── App Settings panel (full-screen) ─────────────────────────────────────────

void CoreGui::DrawSettingsPanel() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;

    // Back on Escape
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        m_settingsOpen = false;

    // ── Full-screen background window ─────────────────────────────────────────
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize({W, H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.07f, 0.07f, 0.10f, 1.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
    ImGui::Begin("##settingsFull", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Sidebar ───────────────────────────────────────────────────────────────
    const float SW = 240.f;

    dl->AddRectFilled({0.f, 0.f}, {SW, H}, IM_COL32(8, 8, 14, 255));
    dl->AddLine({SW, 0.f}, {SW, H}, IM_COL32(38, 38, 60, 200), 1.f);

    // ── Back button ───────────────────────────────────────────────────────────
    const float backH = 44.f;
    const float backY = 18.f;
    ImGui::SetCursorPos({0.f, backY});
    bool backClicked = ImGui::InvisibleButton("##back", {SW, backH});
    bool backHov     = ImGui::IsItemHovered();
    if (backClicked) m_settingsOpen = false;

    if (backHov)
        dl->AddRectFilled({0.f, backY}, {SW, backY + backH}, IM_COL32(255, 255, 255, 10));

    // Left-arrow chevron
    {
        float ax = 22.f, ay = backY + backH * 0.5f;
        float aw = 8.f,  ah = 7.f;
        ImU32 ac = backHov ? IM_COL32(140, 180, 255, 255) : IM_COL32(120, 120, 150, 220);
        dl->AddLine({ax + aw, ay - ah}, {ax,       ay     }, ac, 2.0f);
        dl->AddLine({ax,       ay     }, {ax + aw, ay + ah}, ac, 2.0f);
    }
    // "Back" label
    {
        const char* lbl = "Back";
        ImVec2 tsz = ImGui::CalcTextSize(lbl);
        ImU32  tc  = backHov ? IM_COL32(180, 210, 255, 255) : IM_COL32(140, 140, 170, 220);
        dl->AddText({38.f, backY + (backH - tsz.y) * 0.5f}, tc, lbl);
    }

    // Thin separator line below back button
    dl->AddLine({14.f, backY + backH + 10.f}, {SW - 14.f, backY + backH + 10.f},
                IM_COL32(38, 38, 60, 180), 1.f);

    // "Settings" title
    {
        const float titleY = backY + backH + 26.f;
        ImGui::SetCursorPos({18.f, titleY});
        if (m_fontTitle) ImGui::PushFont(m_fontTitle);
        ImGui::PushStyleColor(ImGuiCol_Text, {0.88f, 0.88f, 0.94f, 1.f});
        ImGui::TextUnformatted("Settings");
        ImGui::PopStyleColor();
        if (m_fontTitle) ImGui::PopFont();
    }

    // ── Nav items ─────────────────────────────────────────────────────────────
    static const char* kNav[] = { "Account", "Appearance", "Privacy", "About" };
    constexpr int kNavCount   = 4;
    const float   itemH       = 44.f;
    const float   navStartY   = backY + backH + 26.f +
                                (m_fontTitle
                                    ? ImGui::CalcTextSize("Settings").y * (30.f / 17.f)
                                    : ImGui::GetTextLineHeight())
                                + 18.f;

    for (int i = 0; i < kNavCount; ++i) {
        float iy  = navStartY + i * (itemH + 3.f);
        bool  sel = (m_settingsTab == i);

        ImGui::SetCursorPos({0.f, iy});
        char bid[16]; snprintf(bid, sizeof(bid), "##sn%d", i);
        bool clicked = ImGui::InvisibleButton(bid, {SW, itemH});
        bool hov     = ImGui::IsItemHovered();
        if (clicked) m_settingsTab = i;

        if (sel)
            dl->AddRectFilled({0.f, iy}, {SW, iy + itemH}, IM_COL32(28, 92, 240, 28));
        else if (hov)
            dl->AddRectFilled({0.f, iy}, {SW, iy + itemH}, IM_COL32(255, 255, 255, 10));

        // Active accent bar on the right edge
        if (sel)
            dl->AddRectFilled({SW - 3.f, iy}, {SW, iy + itemH},
                              IM_COL32(28, 92, 240, 255), 2.f);

        ImVec2 tsz = ImGui::CalcTextSize(kNav[i]);
        ImU32  tc  = sel ? IM_COL32(128, 184, 255, 255) : IM_COL32(160, 160, 185, 230);
        dl->AddText({22.f, iy + (itemH - tsz.y) * 0.5f}, tc, kNav[i]);
    }

    // ── Content area ──────────────────────────────────────────────────────────
    // Content column: centred within the right half, capped at 640 px wide
    const float CX     = SW + 1.f;
    const float CW_raw = W - CX;
    const float kColW  = std::min(CW_raw - 80.f, 640.f);  // readable column width
    const float colOff = (CW_raw - kColW) * 0.5f;          // left offset within child

    ImGui::SetCursorPos({CX, 0.f});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.f, 0.f, 0.f, 0.f});
    ImGui::BeginChild("##settingsContent", {CW_raw, H}, false);

    // Column padding so all text / widgets start at the same X
    const float pad    = colOff + 4.f;
    const float CW     = kColW;   // width used by layout helpers

    ImGui::SetCursorPos({pad, 40.f});

    const ImVec4 kLabel = {0.55f, 0.57f, 0.65f, 1.f};
    const ImVec4 kValue = {0.88f, 0.88f, 0.94f, 1.f};

    // Helper: horizontal rule spanning the column
    auto HRule = [&]() {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
        ImGui::SetCursorPosX(pad);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            {p.x, p.y}, {p.x + CW, p.y},
            IM_COL32(35, 35, 55, 200), 1.f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
    };

    // Helper: right-aligned toggle switch inside the column
    auto Toggle = [&](const char* id, bool v) -> bool {
        const float tw = 40.f, th = 22.f, tr = 11.f;
        ImGui::PushID(id);
        // Position: right edge of column
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 tp = { origin.x + CW - tw, origin.y };
        ImGui::InvisibleButton("##tgl", {tw, th});
        if (ImGui::IsItemClicked()) v = !v;
        ImDrawList* tdl = ImGui::GetWindowDrawList();
        ImU32 bg  = v ? IM_COL32(28, 92, 240, 220) : IM_COL32(50, 50, 70, 200);
        tdl->AddRectFilled(tp, {tp.x + tw, tp.y + th}, bg, tr);
        float knobX = v ? tp.x + tw - tr : tp.x + tr;
        tdl->AddCircleFilled({knobX, tp.y + th * 0.5f}, tr - 3.f, IM_COL32(230, 230, 245, 255));
        ImGui::PopID();
        return v;
    };

    // ── ProfileRow: shared helper for editable / read-only account rows ─────────
    // isEditable=false  → shows "Cannot change" on the right, no button
    // showRemove=true   → shows a × button when the field has a value
    auto ProfileRow = [&](
        const char*  labelText,
        const char*  placeholder,   // dim text shown when value is empty
        std::string& value,
        bool&        editing,
        char*        buf,
        int          bufSz,
        bool         isEditable,
        bool         showRemove
    ) {
        const float kEditW   = 52.f;   // "Edit" / "Add" button
        const float kRemW    = 28.f;   // "×" button
        const float kSaveW   = 56.f;
        const float kCancelW = 70.f;
        const float kGap     = 6.f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.f);

        if (!editing || !isEditable) {
            // ── Display mode ──────────────────────────────────────────────────
            ImGui::SetCursorPosX(pad);
            ImGui::PushStyleColor(ImGuiCol_Text, kLabel);
            ImGui::TextUnformatted(labelText);
            ImGui::PopStyleColor();

            bool hasVal = !value.empty();
            const char* dispVal = hasVal ? value.c_str() : placeholder;
            ImVec4 valCol = hasVal ? kValue : ImVec4(0.38f, 0.38f, 0.48f, 1.f);

            if (isEditable) {
                bool hasRem   = showRemove && hasVal;
                float btnRight = pad + CW;
                float remRight = hasRem ? btnRight - 0.f              : 0.f;
                float btnLeft  = hasRem ? btnRight - kRemW - kGap - kEditW
                                        : btnRight - kEditW;

                // Value text: right of label, left of buttons
                float valRight = btnLeft - 10.f;
                float valLeft  = valRight - ImGui::CalcTextSize(dispVal).x;
                ImGui::SameLine(std::max(valLeft, pad + ImGui::CalcTextSize(labelText).x + 12.f));
                ImGui::PushStyleColor(ImGuiCol_Text, valCol);
                ImGui::TextUnformatted(dispVal);
                ImGui::PopStyleColor();

                // Edit / Add button
                ImGui::SameLine(btnLeft);
                char eid[48]; snprintf(eid, sizeof(eid), "%s##e_%s", hasVal ? "Edit" : "Add", labelText);
                ImGui::PushStyleColor(ImGuiCol_Button,        {0.13f, 0.13f, 0.20f, 1.f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f, 0.22f, 0.34f, 1.f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.09f, 0.09f, 0.14f, 1.f});
                if (ImGui::Button(eid, {kEditW, 0.f})) {
                    editing = true;
                    strncpy(buf, value.c_str(), bufSz - 1);
                    buf[bufSz - 1] = '\0';
                }
                ImGui::PopStyleColor(3);

                // Remove button
                if (hasRem) {
                    ImGui::SameLine(pad + CW - kRemW);
                    char rid[48]; snprintf(rid, sizeof(rid), "x##r_%s", labelText);
                    ImGui::PushStyleColor(ImGuiCol_Button,        {0.22f, 0.05f, 0.05f, 1.f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.38f, 0.09f, 0.09f, 1.f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.14f, 0.03f, 0.03f, 1.f});
                    if (ImGui::Button(rid, {kRemW, 0.f})) {
                        value.clear();
                        SaveProfile();
                    }
                    ImGui::PopStyleColor(3);
                }
            } else {
                // Read-only value
                ImGui::SameLine(pad + CW - ImGui::CalcTextSize(dispVal).x
                                       - ImGui::CalcTextSize("Cannot change").x - 14.f);
                ImGui::PushStyleColor(ImGuiCol_Text, kValue);
                ImGui::TextUnformatted(dispVal);
                ImGui::PopStyleColor();
                ImGui::SameLine(pad + CW - ImGui::CalcTextSize("Cannot change").x);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.32f, 0.32f, 0.42f, 1.f));
                ImGui::TextUnformatted("Cannot change");
                ImGui::PopStyleColor();
            }
        } else {
            // ── Edit mode ─────────────────────────────────────────────────────
            ImGui::SetCursorPosX(pad);
            ImGui::PushStyleColor(ImGuiCol_Text, kLabel);
            ImGui::TextUnformatted(labelText);
            ImGui::PopStyleColor();

            ImGui::SetCursorPosX(pad);
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        {0.09f, 0.09f, 0.14f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, {0.12f, 0.12f, 0.18f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  {0.07f, 0.07f, 0.11f, 1.f});
            float inputW = CW - kSaveW - kCancelW - kGap * 2.f - 2.f;
            ImGui::SetNextItemWidth(inputW);
            char iid[48]; snprintf(iid, sizeof(iid), "##inp_%s", labelText);
            bool enter = ImGui::InputText(iid, buf, bufSz, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor(3);

            ImGui::SameLine(0.f, kGap);
            ImGui::PushStyleColor(ImGuiCol_Button,
                {m_appSettings.accentR,        m_appSettings.accentG,        m_appSettings.accentB,        1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                {m_appSettings.accentR * 1.15f, m_appSettings.accentG * 1.15f, m_appSettings.accentB * 1.15f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                {m_appSettings.accentR * 0.80f, m_appSettings.accentG * 0.80f, m_appSettings.accentB * 0.80f, 1.f});
            char sid[48]; snprintf(sid, sizeof(sid), "Save##sv_%s", labelText);
            if (ImGui::Button(sid, {kSaveW, 0.f}) || enter) {
                value   = buf;
                editing = false;
                SaveProfile();
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine(0.f, kGap);
            ImGui::PushStyleColor(ImGuiCol_Button,        {0.12f, 0.12f, 0.18f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.20f, 0.20f, 0.30f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.08f, 0.08f, 0.12f, 1.f});
            char cid[48]; snprintf(cid, sizeof(cid), "Cancel##c_%s", labelText);
            if (ImGui::Button(cid, {kCancelW, 0.f}))
                editing = false;
            ImGui::PopStyleColor(3);
        }

        ImGui::PopStyleVar();  // FrameRounding
        HRule();
    };

    if (m_settingsTab == 0) {
        // ── Account ───────────────────────────────────────────────────────────
        ImGui::SetCursorPosX(pad);
        ImGui::PushStyleColor(ImGuiCol_Text, kValue);
        ImGui::TextUnformatted("Account");
        ImGui::PopStyleColor();
        HRule();

        // Display Name (editable)
        ProfileRow("Display Name", "Not set",
                   m_displayName, m_editingDisplayName,
                   m_inputDisplayName, (int)sizeof(m_inputDisplayName),
                   true, false);

        // Username (read-only) — use a local so ProfileRow can't mutate it
        { bool _unused = false; char _buf[4] = {};
          ProfileRow("Username", m_username.c_str(),
                     m_username, _unused, _buf, 1,
                     false, false); }

        // Email (optional)
        ProfileRow("Email", "Not set",
                   m_email, m_editingEmail,
                   m_inputEmail, (int)sizeof(m_inputEmail),
                   true, true);

        // Phone number (optional)
        ProfileRow("Phone number", "Not set",
                   m_phoneNumber, m_editingPhone,
                   m_inputPhone, (int)sizeof(m_inputPhone),
                   true, true);

        // Sign out
        ImGui::SetCursorPosX(pad);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.25f, 0.05f, 0.05f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.40f, 0.08f, 0.08f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.18f, 0.04f, 0.04f, 1.f});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
        if (ImGui::Button("Sign Out", {CW, 40.f})) {
            ClearSession();
            m_settingsOpen = false;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

    } else if (m_settingsTab == 1) {
        // ── Appearance ────────────────────────────────────────────────────────
        ImGui::SetCursorPosX(pad);
        ImGui::PushStyleColor(ImGuiCol_Text, kValue);
        ImGui::TextUnformatted("Appearance");
        ImGui::PopStyleColor();
        HRule();

        // Accent colour
        ImGui::SetCursorPosX(pad);
        ImGui::PushStyleColor(ImGuiCol_Text, kLabel);
        ImGui::TextUnformatted("Accent colour");
        ImGui::PopStyleColor();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
        ImGui::SetCursorPosX(pad);

        static const ImVec4 kAccents[] = {
            {0.11f, 0.36f, 0.94f, 1.f},  // blue (default)
            {0.08f, 0.68f, 0.44f, 1.f},  // teal
            {0.60f, 0.18f, 0.90f, 1.f},  // purple
            {0.94f, 0.36f, 0.11f, 1.f},  // orange
            {0.90f, 0.18f, 0.28f, 1.f},  // red
            {0.14f, 0.62f, 0.10f, 1.f},  // green
        };
        bool accentChanged = false;
        for (int k = 0; k < 6; ++k) {
            if (k > 0) ImGui::SameLine(0.f, 10.f);
            ImGui::PushID(k);
            const ImVec4& ac = kAccents[k];
            bool isCur = fabsf(m_appSettings.accentR - ac.x) < 0.01f &&
                         fabsf(m_appSettings.accentG - ac.y) < 0.01f &&
                         fabsf(m_appSettings.accentB - ac.z) < 0.01f;
            ImGui::PushStyleColor(ImGuiCol_Button,
                isCur ? ac : ImVec4(ac.x*0.50f, ac.y*0.50f, ac.z*0.50f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ac);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(ac.x*0.8f, ac.y*0.8f, ac.z*0.8f, 1.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 22.f);
            if (ImGui::Button("##ac", {32.f, 32.f})) {
                m_appSettings.accentR = ac.x;
                m_appSettings.accentG = ac.y;
                m_appSettings.accentB = ac.z;
                accentChanged = true;
            }
            if (isCur) {
                ImVec2 bmin = ImGui::GetItemRectMin();
                ImVec2 bmax = ImGui::GetItemRectMax();
                ImVec2 bctr = {(bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f};
                ImGui::GetWindowDrawList()->AddCircle(bctr, 18.f, IM_COL32(255, 255, 255, 200), 28, 2.2f);
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            ImGui::PopID();
        }
        if (accentChanged) SaveAppSettings();
        HRule();

        // UI Scale
        ImGui::SetCursorPosX(pad);
        ImGui::PushStyleColor(ImGuiCol_Text, kLabel);
        ImGui::TextUnformatted("UI Scale");
        ImGui::PopStyleColor();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
        ImGui::SetCursorPosX(pad);

        static const float kScales[]     = {0.85f, 1.0f, 1.15f, 1.30f};
        static const char* kScaleLbls[]  = {"Small", "Default", "Large", "XL"};
        bool scaleChanged = false;
        for (int k = 0; k < 4; ++k) {
            if (k > 0) ImGui::SameLine(0.f, 8.f);
            ImGui::PushID(100 + k);
            bool isCur = fabsf(m_appSettings.uiScale - kScales[k]) < 0.01f;
            ImGui::PushStyleColor(ImGuiCol_Button,
                isCur ? ImVec4(m_appSettings.accentR, m_appSettings.accentG, m_appSettings.accentB, 1.f)
                      : ImVec4(0.12f, 0.12f, 0.18f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(m_appSettings.accentR*0.75f, m_appSettings.accentG*0.75f, m_appSettings.accentB*0.75f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(m_appSettings.accentR*0.55f, m_appSettings.accentG*0.55f, m_appSettings.accentB*0.55f, 1.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
            if (ImGui::Button(kScaleLbls[k], {90.f, 36.f})) {
                m_appSettings.uiScale = kScales[k];
                scaleChanged = true;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            ImGui::PopID();
        }
        if (scaleChanged) SaveAppSettings();

    } else if (m_settingsTab == 2) {
        // ── Privacy ───────────────────────────────────────────────────────────
        ImGui::SetCursorPosX(pad);
        ImGui::PushStyleColor(ImGuiCol_Text, kValue);
        ImGui::TextUnformatted("Privacy");
        ImGui::PopStyleColor();
        HRule();

        // Helper: one labelled toggle row
        auto ToggleRow = [&](const char* rowId, const char* label, const char* desc, bool& val) {
            // Reserve the row height before drawing so both label and toggle sit on the same baseline
            float rowY = ImGui::GetCursorPosY();
            ImGui::SetCursorPos({pad, rowY});
            ImGui::PushStyleColor(ImGuiCol_Text, kValue);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::SetCursorPosX(pad);
            ImGui::PushStyleColor(ImGuiCol_Text, kLabel);
            ImGui::TextUnformatted(desc);
            ImGui::PopStyleColor();

            // Toggle: vertically centred on the label line
            float th = 22.f;
            float tY = rowY + (ImGui::GetTextLineHeight() - th) * 0.5f;
            ImGui::SetCursorPos({pad + CW - 40.f, tY});
            bool nv = Toggle(rowId, val);
            if (nv != val) { val = nv; SaveAppSettings(); }

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f);
            HRule();
        };

        bool showNT = m_appSettings.showNametags;
        ToggleRow("nametags", "Show player nametags",
                  "Display floating names above other players", showNT);
        m_appSettings.showNametags = showNT;

        bool showOS = m_appSettings.showOnlineStatus;
        ToggleRow("online", "Show online status",
                  "Show the Online/Offline pill in-game", showOS);
        m_appSettings.showOnlineStatus = showOS;

    } else {
        // ── About ─────────────────────────────────────────────────────────────
        ImGui::SetCursorPosX(pad);
        ImGui::PushStyleColor(ImGuiCol_Text, kValue);
        ImGui::TextUnformatted("About");
        ImGui::PopStyleColor();
        HRule();

        auto AboutRow = [&](const char* key, const char* val) {
            ImGui::SetCursorPosX(pad);
            ImGui::PushStyleColor(ImGuiCol_Text, kLabel);
            ImGui::TextUnformatted(key);
            ImGui::PopStyleColor();
            ImGui::SameLine(pad + CW - ImGui::CalcTextSize(val).x);
            ImGui::PushStyleColor(ImGuiCol_Text, kValue);
            ImGui::TextUnformatted(val);
            ImGui::PopStyleColor();
            HRule();
        };

        AboutRow("App",      "CreateToPlay");
        AboutRow("Version",  "0.1.0");
        AboutRow("Build",    __DATE__);
        AboutRow("Engine",   "C++ / OpenGL 3.3");
        AboutRow("Physics",  "Bullet3 3.25");
        AboutRow("UI",       "Dear ImGui 1.91");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();  // ChildBg

    ImGui::End();
    ImGui::PopStyleVar(2);   // WindowBorderSize, WindowPadding
    ImGui::PopStyleColor();  // WindowBg
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
