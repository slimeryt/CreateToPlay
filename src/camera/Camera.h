#pragma once
#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>

class PhysicsWorld;

class Camera {
public:
    void Init(float fovDeg, float aspect, float near, float far);
    void UpdateOrbit(float dx, float dy);
    void Zoom(float delta);  // positive = zoom in, negative = zoom out
    // physics + exclude allow the camera to pull forward when geometry is in the way
    void FollowTarget(const glm::vec3& targetPos,
                      PhysicsWorld* physics = nullptr,
                      const btCollisionObject* exclude = nullptr);
    void SetAspect(float aspect);

    const glm::mat4& GetView()       const { return m_view; }
    const glm::mat4& GetProjection() const { return m_projection; }
    const glm::vec3& GetPosition()   const { return m_position; }
    float            GetYaw()        const { return m_yaw; }

private:
    glm::mat4 m_view       = glm::mat4(1.0f);
    glm::mat4 m_projection = glm::mat4(1.0f);
    glm::vec3 m_position   = glm::vec3(0.0f);

    float m_yaw       =  0.0f;
    float m_pitch     = -0.4f;
    float m_dist      = 14.0f;
    float m_fov       = 60.0f;
    float m_near      = 0.1f;
    float m_far       = 1000.0f;
    float m_aspect    = 16.0f / 9.0f;
    float m_sensitivity = 0.003f;
};
