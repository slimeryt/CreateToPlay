#include "InputManager.h"

void InputManager::BeginFrame() {
    m_justPressed.clear();
    m_dx     = 0.0f;
    m_dy     = 0.0f;
    m_scroll = 0.0f;
}

void InputManager::HandleEvent(const SDL_Event& e) {
    if (e.type == SDL_QUIT) {
        m_quit = true;
    } else if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
        m_down.insert(e.key.keysym.scancode);
        m_justPressed.insert(e.key.keysym.scancode);
    } else if (e.type == SDL_KEYUP) {
        m_down.erase(e.key.keysym.scancode);
    } else if (e.type == SDL_MOUSEMOTION) {
        m_dx += (float)e.motion.xrel;
        m_dy += (float)e.motion.yrel;
    } else if (e.type == SDL_MOUSEWHEEL) {
        m_scroll += (float)e.wheel.y;
    }
}

bool InputManager::IsKeyDown(SDL_Scancode key) const {
    return m_down.count(key) > 0;
}

bool InputManager::IsKeyJustPressed(SDL_Scancode key) const {
    return m_justPressed.count(key) > 0;
}
