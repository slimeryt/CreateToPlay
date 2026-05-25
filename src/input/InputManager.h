#pragma once
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <unordered_set>

class InputManager {
public:
    void BeginFrame();
    void HandleEvent(const SDL_Event& e);

    bool IsKeyDown(SDL_Scancode key)      const;
    bool IsKeyJustPressed(SDL_Scancode key) const;

    float GetMouseDX()     const { return m_dx; }
    float GetMouseDY()     const { return m_dy; }
    float GetScrollDelta() const { return m_scroll; }
    void  ClearMouseDelta() { m_dx = 0.f; m_dy = 0.f; }
    bool  WantsQuit()  const { return m_quit; }

private:
    std::unordered_set<SDL_Scancode> m_down;
    std::unordered_set<SDL_Scancode> m_justPressed;
    float m_dx     = 0.0f;
    float m_dy     = 0.0f;
    float m_scroll = 0.0f;
    bool  m_quit   = false;
};
