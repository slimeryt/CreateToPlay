#pragma once
#include <glm/glm.hpp>
#include <array>

class Workspace;
class PhysicsWorld;
class BasePart;
class btRigidBody;

// Skin = head + arms color, Shirt = torso color, Pants = legs color.
struct AvatarConfig {
    glm::vec3 skin  = {0.976f, 0.820f, 0.173f}; // noob yellow
    glm::vec3 shirt = {0.059f, 0.420f, 0.690f}; // noob blue
    glm::vec3 pants = {0.110f, 0.529f, 0.047f}; // noob green
};

class Character {
public:
    void Init(Workspace& workspace, PhysicsWorld& physics, const glm::vec3& spawnPos);

    void SyncVisuals();
    void SetAvatar(const AvatarConfig& cfg);

    // Advance animation state; call once per FixedUpdate before SyncVisuals.
    // vertVel = rigid-body linear velocity Y (positive = rising, negative = falling).
    void AdvanceAnimation(float dt, bool moving, float vertVel = 0.f);

    glm::vec3    GetPosition() const;
    btRigidBody* GetBody()     const { return m_capsuleBody; }
    float        GetFacingYaw() const { return m_facingYaw; }
    void         SetFacingYaw(float y) { m_facingYaw = y; }

private:
    btRigidBody* m_capsuleBody = nullptr;

    // 6 visual parts: torso, head, armL, armR, legL, legR
    std::array<BasePart*, 6> m_visualParts = {};

    // local offsets and sizes (before facing rotation)
    struct PartDef { glm::vec3 offset; glm::vec3 size; glm::vec3 color; };
    static const std::array<PartDef, 6> kPartDefs;

    float m_facingYaw   = 0.0f;
    float m_walkPhase   = 0.0f;  // radians, drives arm/leg swing while grounded

    // Air animation blend (0 = grounded, 1 = fully airborne)
    float m_jumpBlend   = 0.0f;
    // Current blended air-pose angles (lerp toward target each tick)
    float m_armAirPose  = 0.0f;  // arm X-rotation in air (neg = back/jump, pos = fwd/fall)
    float m_legAirPose  = 0.0f;  // leg X-rotation in air
};
