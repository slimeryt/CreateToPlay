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

    void ToggleShiftLock()        { m_shiftLock = !m_shiftLock; }
    bool IsShiftLocked() const    { return m_shiftLock; }

    // Disable all movement input (e.g. while chat is open)
    void SetInputEnabled(bool on) { m_inputEnabled = on; }

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
    bool  m_shiftLock       = false;
    bool  m_inputEnabled    = true;
};
