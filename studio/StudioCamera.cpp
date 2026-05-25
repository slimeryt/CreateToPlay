#include "StudioCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

void StudioCamera::Init(int vpW, int vpH) {
    m_vpW = vpW; m_vpH = vpH;
    Rebuild();
}

void StudioCamera::SetViewportSize(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    m_vpW = w; m_vpH = h;
    Rebuild();
}

void StudioCamera::HandleEvent(const SDL_Event& e, bool viewportHovered, bool allowKeys) {
    switch (e.type) {
    case SDL_MOUSEBUTTONDOWN:
        if (!viewportHovered) break;
        if (e.button.button == SDL_BUTTON_RIGHT)  m_rmb = true;
        if (e.button.button == SDL_BUTTON_MIDDLE) m_mmb = true;
        break;
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_RIGHT)  m_rmb = false;
        if (e.button.button == SDL_BUTTON_MIDDLE) m_mmb = false;
        break;
    case SDL_MOUSEMOTION:
        if (m_rmb || m_mmb) {
            m_dx += e.motion.xrel;
            m_dy += e.motion.yrel;
        }
        break;
    case SDL_MOUSEWHEEL:
        if (viewportHovered) m_scroll += e.wheel.y;
        break;
    case SDL_KEYDOWN:
        if (!allowKeys) break;
        switch (e.key.keysym.scancode) {
        case SDL_SCANCODE_W:      m_wKey  = true; break;
        case SDL_SCANCODE_A:      m_aKey  = true; break;
        case SDL_SCANCODE_S:      m_sKey  = true; break;
        case SDL_SCANCODE_D:      m_dKey  = true; break;
        case SDL_SCANCODE_Q:      m_qKey  = true; break;
        case SDL_SCANCODE_E:      m_eKey  = true; break;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: m_shift = true; break;
        default: break;
        }
        break;
    case SDL_KEYUP:
        switch (e.key.keysym.scancode) {
        case SDL_SCANCODE_W:      m_wKey  = false; break;
        case SDL_SCANCODE_A:      m_aKey  = false; break;
        case SDL_SCANCODE_S:      m_sKey  = false; break;
        case SDL_SCANCODE_D:      m_dKey  = false; break;
        case SDL_SCANCODE_Q:      m_qKey  = false; break;
        case SDL_SCANCODE_E:      m_eKey  = false; break;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: m_shift = false; break;
        default: break;
        }
        break;
    }
}

void StudioCamera::Update(float dt, bool active) {
    // Compute current forward and right vectors
    float sy = std::sin(m_yaw),   cy = std::cos(m_yaw);
    float sp = std::sin(m_pitch), cp = std::cos(m_pitch);
    glm::vec3 fwd   = { cp * sy, sp, -cp * cy };
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 up    = glm::cross(right, fwd);

    // RMB: look only
    if (m_rmb && active) {
        m_yaw   += m_dx * kSens;
        m_pitch -= m_dy * kSens;
        m_pitch  = std::max(-1.55f, std::min(1.55f, m_pitch));
    }

    // WASD + Q/E: fly whenever the viewport is active (no RMB required).
    if (active) {
        float spd = kSpeed * (m_shift ? 3.f : 1.f) * dt;
        if (m_wKey) m_pos += fwd   * spd;
        if (m_sKey) m_pos -= fwd   * spd;
        if (m_dKey) m_pos += right * spd;
        if (m_aKey) m_pos -= right * spd;
        if (m_eKey) m_pos.y += spd;
        if (m_qKey) m_pos.y -= spd;
    }

    // Middle mouse: pan
    if (m_mmb && active) {
        float panScale = 0.04f;
        m_pos -= right * (m_dx * panScale);
        m_pos += up    * (m_dy * panScale);
    }

    // Scroll: dolly forward/back
    if (active && std::abs(m_scroll) > 0.01f) {
        m_pos += fwd * (m_scroll * 3.f);
    }

    m_dx = m_dy = m_scroll = 0.f;
    Rebuild();
}

void StudioCamera::Rebuild() {
    float sy = std::sin(m_yaw),   cy = std::cos(m_yaw);
    float sp = std::sin(m_pitch), cp = std::cos(m_pitch);
    glm::vec3 fwd = { cp * sy, sp, -cp * cy };

    glm::vec3 worldUp = { 0.f, 1.f, 0.f };
    if (std::abs(fwd.y) > 0.98f) worldUp = { 0.f, 0.f, (fwd.y > 0 ? -1.f : 1.f) };

    m_view = glm::lookAt(m_pos, m_pos + fwd, worldUp);
    float asp = (float)m_vpW / (float)m_vpH;
    m_proj = glm::perspective(glm::radians(m_fov), asp, 0.1f, 2000.f);
}
