#pragma once
#include <glm/glm.hpp>
#include <array>

class Workspace;
class PhysicsWorld;
class BasePart;
class btRigidBody;

class Character {
public:
    void Init(Workspace& workspace, PhysicsWorld& physics, const glm::vec3& spawnPos);

    void SyncVisuals();

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

    float m_facingYaw = 0.0f;
};
