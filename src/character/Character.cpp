#include "Character.h"
#include "scene/Workspace.h"
#include "physics/PhysicsWorld.h"
#include "physics/PhysicsUtils.h"
#include "scene/BasePart.h"
#include <btBulletDynamicsCommon.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

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
    m_physics     = &physics;
    m_capsuleBody = physics.CreateCapsuleBody(0.7f, 2.8f, 1.0f, spawnPos);

    m_capsuleBody->setAngularFactor(btVector3(0, 0, 0));
    m_capsuleBody->setActivationState(DISABLE_DEACTIVATION);
    m_capsuleBody->setFriction(0.0f);
    m_capsuleBody->setDamping(0.0f, 0.0f);
    m_capsuleBody->setCcdMotionThreshold(0.14f);
    m_capsuleBody->setCcdSweptSphereRadius(0.56f);

    // Create 6 visual parts + a matching kinematic hitbox for each
    for (int i = 0; i < 6; ++i) {
        auto part = std::make_unique<BasePart>();
        part->name        = "CharPart" + std::to_string(i);
        part->size        = kPartDefs[i].size;
        part->color       = kPartDefs[i].color;
        part->reflectance = 0.05f;
        m_visualParts[i]  = workspace.AddPart(std::move(part));

        // Box hitbox sized to the visual part (half-extents = size/2)
        glm::vec3 half = kPartDefs[i].size * 0.5f;
        btRigidBody* hb = physics.CreateBoxBody(half, 0.0f, spawnPos + kPartDefs[i].offset);
        hb->setCollisionFlags(hb->getCollisionFlags()
            | btCollisionObject::CF_KINEMATIC_OBJECT
            | btCollisionObject::CF_NO_CONTACT_RESPONSE);
        hb->setActivationState(DISABLE_DEACTIVATION);
        m_partBodies[i] = hb;
    }

    SyncVisuals();
}

void Character::Shutdown() {
    for (int i = 0; i < 6; ++i) {
        if (m_partBodies[i] && m_physics) {
            m_physics->RemoveBody(m_partBodies[i]);
            m_partBodies[i] = nullptr;
        }
    }
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

void Character::AdvanceAnimation(float dt, bool moving, float vertVel) {
    constexpr float kPi2 = 6.28318f;

    // ── Air blend ──────────────────────────────────────────────────────────────
    // Consider airborne when vertical speed exceeds a small threshold
    bool inAir       = std::abs(vertVel) > 2.5f;
    float blendRate  = inAir ? 14.0f : 9.0f;   // pop into air fast, ease out on land
    float blendTgt   = inAir ? 1.0f  : 0.0f;
    m_jumpBlend += (blendTgt - m_jumpBlend) * (1.f - std::exp(-blendRate * dt));

    // ── Air pose targets ───────────────────────────────────────────────────────
    // Jump (rising):  arms swept back, legs slightly back
    // Fall (falling): arms forward/out, legs hanging forward
    bool rising       = vertVel > 2.5f;
    float armTarget   = rising ? -0.85f :  0.50f;
    float legTarget   = rising ? -0.20f :  0.18f;
    float poseRate    = 10.0f;
    m_armAirPose += (armTarget - m_armAirPose) * (1.f - std::exp(-poseRate * dt));
    m_legAirPose += (legTarget - m_legAirPose) * (1.f - std::exp(-poseRate * dt));

    // ── Walk cycle (only advances when grounded) ───────────────────────────────
    float groundFactor = 1.0f - m_jumpBlend;
    if (moving && groundFactor > 0.2f) {
        m_walkPhase += dt * 7.0f;
        if (m_walkPhase > kPi2) m_walkPhase -= kPi2;
    } else {
        m_walkPhase *= std::exp(-8.0f * dt);
    }
}

// Helper: push a world-space pos+rot into a kinematic hitbox body's motion state.
static void SyncHitbox(btRigidBody* body, const glm::vec3& pos, const glm::quat& rot) {
    if (!body) return;
    btTransform t;
    t.setIdentity();
    t.setOrigin(btVector3(pos.x, pos.y, pos.z));
    t.setRotation(btQuaternion(rot.x, rot.y, rot.z, rot.w));
    body->getMotionState()->setWorldTransform(t);
    body->setWorldTransform(t);  // also set directly for same-frame ray queries
}

void Character::SyncVisuals() {
    glm::vec3 capsulePos = GetPosition();
    glm::quat facingRot  = glm::angleAxis(m_facingYaw, glm::vec3(0, 1, 0));

    // Walk swing fades to zero as we become airborne
    float groundFactor = 1.0f - m_jumpBlend;
    float swing        = std::sin(m_walkPhase) * 0.50f * groundFactor;

    // ── Static parts: torso (0), head (1) ─────────────────────────────────────
    for (int i = 0; i < 2; ++i) {
        glm::vec3 wp = capsulePos + facingRot * kPartDefs[i].offset;
        m_visualParts[i]->position = wp;
        m_visualParts[i]->rotation = facingRot;
        SyncHitbox(m_partBodies[i], wp, facingRot);
    }

    // ── Arms (2 = L, 3 = R) — pivot at shoulder ───────────────────────────────
    static const glm::vec3 kArmPivot[2] = {{-1.12f, 0.84f, 0.f}, {1.12f, 0.84f, 0.f}};
    const float walkSwing[2] = { swing, -swing };
    for (int i = 0; i < 2; ++i) {
        float angle    = walkSwing[i] + m_armAirPose * m_jumpBlend;
        glm::quat rot  = glm::angleAxis(angle, glm::vec3(1, 0, 0));
        glm::vec3 lpos = kArmPivot[i] + rot * (kPartDefs[2 + i].offset - kArmPivot[i]);
        glm::vec3 wp   = capsulePos + facingRot * lpos;
        glm::quat wr   = facingRot * rot;
        m_visualParts[2 + i]->position = wp;
        m_visualParts[2 + i]->rotation = wr;
        SyncHitbox(m_partBodies[2 + i], wp, wr);
    }

    // ── Legs (4 = L, 5 = R) — pivot at hip ────────────────────────────────────
    static const glm::vec3 kLegPivot[2] = {{-0.385f, -0.63f, 0.f}, {0.385f, -0.63f, 0.f}};
    const float legWalkSwing[2] = { -swing, swing };
    for (int i = 0; i < 2; ++i) {
        float angle    = legWalkSwing[i] + m_legAirPose * m_jumpBlend;
        glm::quat rot  = glm::angleAxis(angle, glm::vec3(1, 0, 0));
        glm::vec3 lpos = kLegPivot[i] + rot * (kPartDefs[4 + i].offset - kLegPivot[i]);
        glm::vec3 wp   = capsulePos + facingRot * lpos;
        glm::quat wr   = facingRot * rot;
        m_visualParts[4 + i]->position = wp;
        m_visualParts[4 + i]->rotation = wr;
        SyncHitbox(m_partBodies[4 + i], wp, wr);
    }
}
