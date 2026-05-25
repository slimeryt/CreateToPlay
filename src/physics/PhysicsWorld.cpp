#include "PhysicsWorld.h"
#include "PhysicsUtils.h"
#include <algorithm>

void PhysicsWorld::Init() {
    m_config     = std::make_unique<btDefaultCollisionConfiguration>();
    m_dispatcher = std::make_unique<btCollisionDispatcher>(m_config.get());
    m_broadphase = std::make_unique<btDbvtBroadphase>();
    m_solver     = std::make_unique<btSequentialImpulseConstraintSolver>();
    m_world      = std::make_unique<btDiscreteDynamicsWorld>(
        m_dispatcher.get(), m_broadphase.get(), m_solver.get(), m_config.get());

    // Roblox-style: 20× Earth gravity for snappy feel
    m_world->setGravity(btVector3(0.0f, -196.2f, 0.0f));
}

void PhysicsWorld::Shutdown() {
    m_bodies.clear();
    m_motionStates.clear();
    m_shapes.clear();
    m_world.reset();
    m_solver.reset();
    m_broadphase.reset();
    m_dispatcher.reset();
    m_config.reset();
}

void PhysicsWorld::Step(float dt) {
    m_world->stepSimulation(dt, 10, 1.0f / 120.0f);
}

btRigidBody* PhysicsWorld::AddBody(btCollisionShape* shape, float mass, const glm::vec3& pos) {
    btVector3 inertia(0, 0, 0);
    if (mass > 0.0f)
        shape->calculateLocalInertia(mass, inertia);

    btTransform t;
    t.setIdentity();
    t.setOrigin(GlmToBt(pos));

    auto ms = std::make_unique<btDefaultMotionState>(t);
    btRigidBody::btRigidBodyConstructionInfo ci(mass, ms.get(), shape, inertia);
    auto body = std::make_unique<btRigidBody>(ci);

    btRigidBody* raw = body.get();
    m_world->addRigidBody(raw);

    m_motionStates.push_back(std::move(ms));
    m_bodies.push_back(std::move(body));
    return raw;
}

btRigidBody* PhysicsWorld::CreateBoxBody(const glm::vec3& halfExtents, float mass,
                                           const glm::vec3& pos) {
    auto shape = std::make_unique<btBoxShape>(GlmToBt(halfExtents));
    btRigidBody* raw = AddBody(shape.get(), mass, pos);
    m_shapes.push_back(std::move(shape));
    return raw;
}

btRigidBody* PhysicsWorld::CreateCapsuleBody(float radius, float height, float mass,
                                               const glm::vec3& pos) {
    auto shape = std::make_unique<btCapsuleShape>(radius, height);
    btRigidBody* raw = AddBody(shape.get(), mass, pos);
    m_shapes.push_back(std::move(shape));
    return raw;
}

void PhysicsWorld::RemoveBody(btRigidBody* body) {
    if (!body) return;
    m_world->removeRigidBody(body);

    auto it = std::find_if(m_bodies.begin(), m_bodies.end(),
        [body](const auto& b) { return b.get() == body; });
    if (it == m_bodies.end()) return;

    // Remove associated motion state
    btMotionState* ms = (*it)->getMotionState();
    auto msIt = std::find_if(m_motionStates.begin(), m_motionStates.end(),
        [ms](const auto& m) { return m.get() == ms; });
    if (msIt != m_motionStates.end()) m_motionStates.erase(msIt);

    // Remove associated collision shape
    btCollisionShape* shape = (*it)->getCollisionShape();
    auto shIt = std::find_if(m_shapes.begin(), m_shapes.end(),
        [shape](const auto& s) { return s.get() == shape; });
    if (shIt != m_shapes.end()) m_shapes.erase(shIt);

    m_bodies.erase(it);
}

float PhysicsWorld::RayCastFraction(const glm::vec3& from, const glm::vec3& to,
                                      const btCollisionObject* exclude) const {
    struct ExcludeCallback : btCollisionWorld::ClosestRayResultCallback {
        const btCollisionObject* m_skip;
        ExcludeCallback(const btVector3& a, const btVector3& b, const btCollisionObject* skip)
            : ClosestRayResultCallback(a, b), m_skip(skip) {}
        btScalar addSingleResult(btCollisionWorld::LocalRayResult& r, bool inWorld) override {
            if (r.m_collisionObject == m_skip) return 1.0f;
            return ClosestRayResultCallback::addSingleResult(r, inWorld);
        }
    };
    btVector3 bFrom = GlmToBt(from);
    btVector3 bTo   = GlmToBt(to);
    ExcludeCallback cb(bFrom, bTo, exclude);
    m_world->rayTest(bFrom, bTo, cb);
    return cb.hasHit() ? cb.m_closestHitFraction : 1.0f;
}

bool PhysicsWorld::RayCast(const glm::vec3& from, const glm::vec3& to,
                             btCollisionObject** hitOut) const {
    btVector3 bFrom = GlmToBt(from);
    btVector3 bTo   = GlmToBt(to);
    btCollisionWorld::ClosestRayResultCallback cb(bFrom, bTo);
    m_world->rayTest(bFrom, bTo, cb);
    if (hitOut) *hitOut = const_cast<btCollisionObject*>(cb.m_collisionObject);
    return cb.hasHit();
}
