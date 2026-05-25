#pragma once
#include <glm/glm.hpp>
#include <SDL.h>

// Free-fly editor camera (viewport focused/hovered).
// RMB + mouse drag  → look around
// WASD              → fly (Shift = 3× speed)
// Q / E             → fly down / up
// Scroll            → dolly forward/back
// Middle mouse drag → pan
class StudioCamera {
public:
    void Init(int vpW, int vpH);
    void HandleEvent(const SDL_Event& e, bool viewportHovered, bool allowKeys);
    void Update(float dt, bool active);
    void SetViewportSize(int w, int h);

    const glm::mat4& GetView()       const { return m_view; }
    const glm::mat4& GetProjection() const { return m_proj; }
    const glm::vec3& GetPosition()   const { return m_pos; }

private:
    void Rebuild();

    glm::mat4 m_view = glm::mat4(1.f);
    glm::mat4 m_proj = glm::mat4(1.f);
    glm::vec3 m_pos  = { 0.f, 12.f, 28.f };

    float m_yaw   =  0.00f;   // radians
    float m_pitch = -0.35f;   // radians (negative = look slightly down)
    float m_fov   = 60.f;
    int   m_vpW   = 1;
    int   m_vpH   = 1;

    bool  m_rmb = false;
    bool  m_mmb = false;
    float m_dx = 0.f, m_dy = 0.f;
    float m_scroll = 0.f;

    bool m_wKey=false, m_aKey=false, m_sKey=false, m_dKey=false;
    bool m_qKey=false, m_eKey=false, m_shift=false;

    static constexpr float kSpeed = 18.f;
    static constexpr float kSens  = 0.003f;
};
