#pragma once
#include <SDL.h>
#include <string>
#include <vector>
#include "core/Window.h"
#include "EditorScene.h"
#include "StudioCamera.h"
#include "StudioGui.h"      // also pulls in GameEntry
#include "StudioRenderer.h"

enum class AppState { Login, Home, Studio };

class StudioApp {
public:
    bool Init();
    void Run();
    void Shutdown();

private:
    void ProcessCameraInput(float dt, const std::vector<SDL_Event>& events);
    void InitStudio();    // deferred: creates renderer/scene/camera on first entry
    void SaveSession();   // persist username to disk
    bool LoadSession();   // returns true if a saved session was found

    Window         m_window;
    EditorScene    m_scene;
    StudioCamera   m_camera;
    StudioRenderer m_renderer;
    StudioGui      m_gui;
    bool           m_running      = false;
    bool           m_studioReady  = false;

    AppState               m_state    = AppState::Login;
    std::string            m_username;
    std::vector<GameEntry> m_games;
};
