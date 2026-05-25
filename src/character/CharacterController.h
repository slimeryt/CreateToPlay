#pragma once

class Character;
class Camera;
class InputManager;
class PhysicsWorld;

class CharacterController {
public:
    void Init(Character* character, Camera* camera,
              InputManager* input, PhysicsWorld* physics);

    void Update(float dt);

private:
    bool IsGrounded() const;

    Character*    m_character = nullptr;
    Camera*       m_camera    = nullptr;
    InputManager* m_input     = nullptr;
    PhysicsWorld* m_physics   = nullptr;

    static constexpr float kMoveSpeed   = 16.0f;
    static constexpr float kJumpSpeed   = 50.0f;
    static constexpr float kGroundRange = 2.45f;

    float m_targetFacingYaw = 0.0f;
};
