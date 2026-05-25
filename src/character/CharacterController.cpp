#include "CharacterController.h"
#include "Character.h"
#include "camera/Camera.h"
#include "input/InputManager.h"
#include "physics/PhysicsWorld.h"
#include "physics/PhysicsUtils.h"
#include <btBulletDynamicsCommon.h>
#include <SDL.h>
#include <glm/glm.hpp>
#include <cmath>

void CharacterController::Init(Character* character, Camera* camera,
                                 InputManager* input, PhysicsWorld* physics) {
    m_character = character;
    m_camera    = camera;
    m_input     = input;
    m_physics   = physics;
}

bool CharacterController::IsGrounded() const {
    glm::vec3 from = m_character->GetPosition();
    glm::vec3 to   = from - glm::vec3(0, kGroundRange, 0);

    btCollisionObject* hit = nullptr;
    if (!m_physics->RayCast(from, to, &hit)) return false;

    // Exclude the capsule itself
    return hit != (btCollisionObject*)m_character->GetBody();
}

void CharacterController::Update(float dt) {
    // Gather input
    float ix = 0.0f, iz = 0.0f;
    if (m_input->IsKeyDown(SDL_SCANCODE_D)) ix += 1.0f;
    if (m_input->IsKeyDown(SDL_SCANCODE_A)) ix -= 1.0f;
    if (m_input->IsKeyDown(SDL_SCANCODE_W)) iz += 1.0f;
    if (m_input->IsKeyDown(SDL_SCANCODE_S)) iz -= 1.0f;

    // Normalize diagonal input
    float len = std::sqrt(ix * ix + iz * iz);
    if (len > 0.001f) { ix /= len; iz /= len; }

    // Rotate input into world space using camera yaw.
    // Camera sits at (sin(yaw), _, cos(yaw)) * dist, so the forward direction
    // the player expects from W is (-sin(yaw), 0, -cos(yaw)).
    float yaw = m_camera->GetYaw();
    float wx =  ix * std::cos(yaw) - iz * std::sin(yaw);
    float wz = -ix * std::sin(yaw) - iz * std::cos(yaw);

    // Smooth velocity — exponential lerp toward target so acceleration feels natural
    btVector3 curVel   = m_character->GetBody()->getLinearVelocity();
    float velAlpha     = 1.0f - std::exp(-14.0f * dt);
    float newVx        = curVel.x() + (wx * kMoveSpeed - curVel.x()) * velAlpha;
    float newVz        = curVel.z() + (wz * kMoveSpeed - curVel.z()) * velAlpha;
    m_character->GetBody()->setLinearVelocity(btVector3(newVx, curVel.y(), newVz));

    // Smooth facing rotation — lerp toward movement direction, not instant snap
    if (len > 0.001f)
        m_targetFacingYaw = std::atan2(wx, wz);

    float current = m_character->GetFacingYaw();
    float diff    = m_targetFacingYaw - current;
    // Wrap to [-π, π] so we always take the short arc
    while (diff >  3.14159f) diff -= 6.28318f;
    while (diff < -3.14159f) diff += 6.28318f;
    float rotAlpha = 1.0f - std::exp(-12.0f * dt);
    m_character->SetFacingYaw(current + diff * rotAlpha);

    // Jump — holding space re-jumps as soon as the character lands
    if (m_input->IsKeyDown(SDL_SCANCODE_SPACE) && IsGrounded()) {
        btVector3 vel = m_character->GetBody()->getLinearVelocity();
        m_character->GetBody()->setLinearVelocity(btVector3(vel.x(), kJumpSpeed, vel.z()));
    }
}
