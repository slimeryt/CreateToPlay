#pragma once
#include <SDL.h>
#include <imgui.h>

class CoreGui {
public:
    void Init(SDL_Window* window, SDL_GLContext glContext);
    void Shutdown();

    void ProcessEvent(const SDL_Event& e);
    void BeginFrame();
    void Render();          // in-game HUD + pause menu
    void RenderHomePage();  // lobby screen (before game starts)

    bool GameStarted()   const { return m_gameStarted; }
    bool IsMenuOpen()    const { return m_menuOpen; }
    void SetMenuOpen(bool open) {
        if (open && !m_menuOpen) m_skipEscapeThisFrame = true;
        m_menuOpen = open;
    }
    bool WantsQuit()     const { return m_wantsQuit; }
    bool WantsLeave()    const { return m_wantsLeave; }
    void ClearLeave() {
        m_wantsLeave       = false;
        m_gameStarted      = false;
        m_menuOpen         = false;
        m_leaveConfirmOpen = false;
    }
    bool WantsReset()    const { return m_wantsReset; }
    void ClearReset()          { m_wantsReset = false; }

    // Server address typed on the home page — "" means offline / solo play
    const char* GetServerAddr() const { return m_serverAddr; }

private:
    void DrawMenuButton();
    void DrawEscapeMenu();
    void DrawHomePage();

    ImFont* m_fontTitle        = nullptr;

    int  m_sidebarTab          = 0;    // 0=Home 1=Avatar 2=More
    bool m_wantsLeave          = false;
    bool m_gameStarted         = false;
    bool m_menuOpen            = false;
    bool m_leaveConfirmOpen    = false;
    bool m_wantsQuit           = false;
    bool m_wantsReset          = false;
    bool m_skipEscapeThisFrame = false;

    char m_serverAddr[128]     = {};   // "host:port" or "host" or empty
};
