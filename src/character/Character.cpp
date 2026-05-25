#include "Character.h"
#include "scene/Workspace.h"
#include "physics/PhysicsWorld.h"
#include "physics/PhysicsUtils.h"
#include "scene/BasePart.h"
#include <btBulletDynamicsCommon.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Roblox R6 proportions scaled to our 3-unit capsule (capsule center = local origin).
// Ground is at -1.5, capsule top at +1.5.
//
// Layout (local Y):
//   head   bottom = +0.60, top = +1.45  (0.85 cube)
//   torso  bottom = -0.40, top = +0.60  (1.0 tall, 1.0 wide, 0.5 deep)
//   arms   same Y-center as torso, flush against torso sides (0.5 × 1.0 × 0.5)
//   legs   bottom = -1.40, top = -0.40  (0.5 × 1.0 × 0.5), meet torso bottom
//
// Colors: classic Roblox "noob" — yellow head/arms, blue torso, green legs.
// All values scaled 1.4x from the base proportions.
// Capsule is also 1.4x (radius=0.7, height=2.8), center at y=2.1 on ground.
const std::array<Character::PartDef, 6> Character::kPartDefs = {{
    {{ 0.000f,  0.14f,  0.0f}, {1.40f, 1.40f, 0.70f}, {0.06f, 0.42f, 0.69f}}, // torso
    {{ 0.000f,  1.51f,  0.0f}, {1.20f, 1.20f, 1.20f}, {0.98f, 0.82f, 0.17f}}, // head
    {{-1.120f,  0.14f,  0.0f}, {0.70f, 1.40f, 0.70f}, {0.98f, 0.82f, 0.17f}}, // armL
    {{ 1.120f,  0.14f,  0.0f}, {0.70f, 1.40f, 0.70f}, {0.98f, 0.82f, 0.17f}}, // armR
    {{-0.385f, -1.33f,  0.0f}, {0.63f, 1.40f, 0.70f}, {0.11f, 0.53f, 0.05f}}, // legL
    {{ 0.385f, -1.33f,  0.0f}, {0.63f, 1.40f, 0.70f}, {0.11f, 0.53f, 0.05f}}, // legR
}};

void Character::Init(Workspace& workspace, PhysicsWorld& physics, const glm::vec3& spawnPos) {
    m_capsuleBody = physics.CreateCapsuleBody(0.7f, 2.8f, 1.0f, spawnPos);

    m_capsuleBody->setAngularFactor(btVector3(0, 0, 0));
    m_capsuleBody->setActivationState(DISABLE_DEACTIVATION);
    m_capsuleBody->setFriction(0.0f);
    m_capsuleBody->setDamping(0.0f, 0.0f);
    m_capsuleBody->setCcdMotionThreshold(0.14f);
    m_capsuleBody->setCcdSweptSphereRadius(0.56f);

    // Create 6 visual parts (no physics body — visual only)
    for (int i = 0; i < 6; ++i) {
        auto part = std::make_unique<BasePart>();
        part->name        = "CharPart" + std::to_string(i);
        part->size        = kPartDefs[i].size;
        part->color       = kPartDefs[i].color;
        part->reflectance = 0.05f;
        m_visualParts[i]  = workspace.AddPart(std::move(part));
    }

    SyncVisuals();
}

glm::vec3 Character::GetPosition() const {
    btTransform t;
    m_capsuleBody->getMotionState()->getWorldTransform(t);
    return BtToGlm(t.getOrigin());
}

void Character::SetAvatar(const AvatarConfig& cfg) {
    // 0=torso(shirt), 1=head(skin), 2=armL(skin), 3=armR(skin), 4=legL(pants), 5=legR(pants)
    if (m_visualParts[0]) m_visualParts[0]->color = cfg.shirt;
    if (m_visualParts[1]) m_visualParts[1]->color = cfg.skin;
    if (m_visualParts[2]) m_visualParts[2]->color = cfg.skin;
    if (m_visualParts[3]) m_visualParts[3]->color = cfg.skin;
    if (m_visualParts[4]) m_visualParts[4]->color = cfg.pants;
    if (m_visualParts[5]) m_visualParts[5]->color = cfg.pants;
}

void Character::SyncVisuals() {
    glm::vec3 capsulePos = GetPosition();
    glm::quat facingRot  = glm::angleAxis(m_facingYaw, glm::vec3(0, 1, 0));

    for (int i = 0; i < 6; ++i) {
        m_visualParts[i]->position = capsulePos + facingRot * kPartDefs[i].offset;
        m_visualParts[i]->rotation = facingRot;
    }
}
