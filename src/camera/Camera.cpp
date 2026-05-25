#include "Camera.h"
#include "../physics/PhysicsWorld.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

static constexpr float kPi = 3.14159265358979f;

void Camera::Init(float fovDeg, float aspect, float near, float far) {
    m_fov    = fovDeg;
    m_aspect = aspect;
    m_near   = near;
    m_far    = far;
    m_projection = glm::perspective(glm::radians(fovDeg), aspect, near, far);
}

void Camera::SetAspect(float aspect) {
    m_aspect     = aspect;
    m_projection = glm::perspective(glm::radians(m_fov), aspect, m_near, m_far);
}


void Camera::Zoom(float delta) {
    m_dist -= delta * 1.5f;
    if (m_dist < 2.0f)  m_dist = 2.0f;
    if (m_dist > 60.0f) m_dist = 60.0f;
}

void Camera::UpdateOrbit(float dx, float dy) {
    m_yaw   -= dx * m_sensitivity;  // negate: mouse right = scene rotates right = camera goes left
    m_pitch -= dy * m_sensitivity;  // negate: SDL y is down-positive, so flip for natural feel
    const float minP = glm::radians(-75.0f);
    const float maxP = glm::radians(80.0f);
    m_pitch = std::max(minP, std::min(maxP, m_pitch));
}

void Camera::FollowTarget(const glm::vec3& targetPos,
                           PhysicsWorld* physics,
                           const btCollisionObject* exclude) {
    glm::vec3 dir;
    dir.x = std::cos(m_pitch) * std::sin(m_yaw);
    dir.y = std::sin(-m_pitch);
    dir.z = std::cos(m_pitch) * std::cos(m_yaw);

    // Snap side offset immediately — shift lock is instant
    m_sideOffset = m_shiftLock ? 1.75f : 0.0f;

    // Right vector in the XZ plane (perpendicular to camera forward)
    glm::vec3 right = glm::vec3(std::cos(m_yaw), 0.0f, -std::sin(m_yaw));

    // Orbit pivot at shoulder height, shifted right when in shift-lock mode
    glm::vec3 pivot   = targetPos + glm::vec3(0.0f, 1.5f, 0.0f) + right * m_sideOffset;
    glm::vec3 desired = pivot + dir * m_dist;

    float useDist = m_dist;
    if (physics) {
        float t = physics->RayCastFraction(pivot, desired, exclude);
        if (t < 1.0f) {
            useDist = m_dist * t - 0.25f;
            if (useDist < 0.5f) useDist = 0.5f;
        }
    }

    m_position = pivot + dir * useDist;
    m_view = glm::lookAt(m_position, pivot, glm::vec3(0.0f, 1.0f, 0.0f));
}
