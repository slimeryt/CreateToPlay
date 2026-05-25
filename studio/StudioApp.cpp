#include "StudioApp.h"
#include <glad/glad.h>
#include <SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Session persistence
// ─────────────────────────────────────────────────────────────────────────────
static std::string SessionPath() {
    char* pref = SDL_GetPrefPath("CreateToPlay", "Studio");
    if (!pref) return {};
    std::string p = std::string(pref) + "session.txt";
    SDL_free(pref);
    return p;
}

void StudioApp::SaveSession() {
    const std::string path = SessionPath();
    if (path.empty()) return;
    std::ofstream f(path);
    if (f) f << m_username;
}

bool StudioApp::LoadSession() {
    const std::string path = SessionPath();
    if (path.empty()) return false;
    std::ifstream f(path);
    if (!f) return false;
    std::getline(f, m_username);
    return !m_username.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
bool StudioApp::Init() {
    if (!m_window.Init("CreateToPlay Studio", 1600, 900, false))
        return false;

    SDL_GL_MakeCurrent(m_window.GetSDLWindow(), m_window.GetGLContext());
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        printf("Studio: failed to load OpenGL\n");
        return false;
    }

    if (!m_gui.Init(m_window.GetSDLWindow(), m_window.GetGLContext()))
        return false;

    // No places by default — user creates them from the Home screen.

    // If a previous session was saved, jump straight to the Home screen.
    if (LoadSession())
        m_state = AppState::Home;

    m_running = true;
    printf("CreateToPlay Studio launcher ready.\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Deferred studio init — runs once when the user first enters the editor.
// ─────────────────────────────────────────────────────────────────────────────
void StudioApp::InitStudio() {
    if (m_studioReady) return;
    if (!m_renderer.Init(800, 600)) {
        printf("Studio: renderer init failed\n");
        return;
    }
    m_camera.Init(800, 600);
    m_scene.CreateDefaultScene();
    m_studioReady = true;
    printf("Studio editor ready.\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera input routing
// ─────────────────────────────────────────────────────────────────────────────
void StudioApp::ProcessCameraInput(float dt,
                                   const std::vector<SDL_Event>& events) {
    ImGuiIO& io = ImGui::GetIO();
    const bool vp        = m_gui.ViewportHovered();
    const bool rmb       = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool mmb       = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    const bool allowKeys = vp && !io.WantTextInput;

    for (const SDL_Event& e : events) {
        bool route = false;
        switch (e.type) {
        case SDL_KEYUP:           route = true;            break;
        case SDL_KEYDOWN:         route = allowKeys;       break;
        case SDL_MOUSEWHEEL:      route = vp;              break;
        case SDL_MOUSEBUTTONUP:   route = true;            break;
        case SDL_MOUSEBUTTONDOWN:
            if (vp) {
                int btn = e.button.button;
                route = (btn == SDL_BUTTON_RIGHT || btn == SDL_BUTTON_MIDDLE);
            }
            break;
        case SDL_MOUSEMOTION:     route = (rmb || mmb);    break;
        default: break;
        }
        if (route) m_camera.HandleEvent(e, vp, allowKeys);
    }
    m_camera.Update(dt, vp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────────────────────────────────────────
void StudioApp::Run() {
    Uint64 last = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    std::vector<SDL_Event> events;
    events.reserve(32);

    while (m_running && !m_window.ShouldClose()) {
        // ── Delta time ───────────────────────────────────────────────────────
        Uint64 now = SDL_GetPerformanceCounter();
        float  dt  = std::min((float)(now - last) / (float)freq, 0.05f);
        last = now;

        // ── Event pump ───────────────────────────────────────────────────────
        events.clear();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            m_gui.ProcessEvent(e);
            if (e.type == SDL_QUIT)
                m_window.SetShouldClose(true);
            if (e.type == SDL_WINDOWEVENT &&
                e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                glViewport(0, 0, e.window.data1, e.window.data2);
            events.push_back(e);
        }

        // ── Frame ────────────────────────────────────────────────────────────
        m_gui.BeginFrame();

        switch (m_state) {

        // ── Login screen ─────────────────────────────────────────────────────
        case AppState::Login: {
            std::string user;
            if (m_gui.DrawLoginScreen(user)) {
                m_username = user;
                m_state    = AppState::Home;
                SaveSession();
            }
            break;
        }

        // ── Home / game-library screen ───────────────────────────────────────
        case AppState::Home: {
            int action = m_gui.DrawHomeScreen(m_username, m_games);
            if (action != 0) {
                InitStudio();
                m_state = AppState::Studio;
            }
            break;
        }

        // ── Studio editor ────────────────────────────────────────────────────
        case AppState::Studio:
            if (m_gui.DrawRibbon(m_scene))
                m_state = AppState::Home;
            m_gui.DrawDockedPanels(m_scene, m_camera, m_renderer);
            ProcessCameraInput(dt, events);
            m_renderer.RenderAll(m_scene, m_camera);
            break;
        }

        // ── Render OS window ─────────────────────────────────────────────────
        const int w = m_window.GetWidth();
        const int h = m_window.GetHeight();
        glViewport(0, 0, w, h);
        if (m_state == AppState::Studio)
            glClearColor(0.15f, 0.15f, 0.16f, 1.f);
        else
            glClearColor(0.10f, 0.10f, 0.11f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_gui.EndFrame();
        m_window.SwapBuffers();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void StudioApp::Shutdown() {
    if (m_studioReady) m_renderer.Shutdown();
    m_gui.Shutdown();
    m_window.Shutdown();
}
