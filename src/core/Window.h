#pragma once
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <string>

class Window {
public:
    bool Init(const std::string& title, int width, int height);
    void Shutdown();
    void SwapBuffers();

    int  GetWidth()  const { return m_width;  }
    int  GetHeight() const { return m_height; }
    bool ShouldClose() const { return m_shouldClose; }
    void SetShouldClose(bool v) { m_shouldClose = v; }
    SDL_Window*   GetSDLWindow()  { return m_window;  }
    SDL_GLContext  GetGLContext()  { return m_context; }

private:
    SDL_Window*   m_window  = nullptr;
    SDL_GLContext m_context = nullptr;
    int  m_width  = 1280;
    int  m_height = 720;
    bool m_shouldClose = false;
};
