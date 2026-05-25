#pragma once
#include <btBulletDynamicsCommon.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class PhysicsWorld {
public:
    void Init();
    void Shutdown();
    void Step(float dt);

    btRigidBody* CreateBoxBody(const glm::vec3& halfExtents, float mass,
                                const glm::vec3& pos = glm::vec3(0.0f));
    btRigidBody* CreateCapsuleBody(float radius, float height, float mass,
                                    const glm::vec3& pos = glm::vec3(0.0f));

    bool RayCast(const glm::vec3& from, const glm::vec3& to,
                 btCollisionObject** hitOut = nullptr) const;

    // Returns the closest hit fraction [0,1] along the ray, skipping `exclude`.
    // Returns 1.0 (no hit) if nothing found.
    float RayCastFraction(const glm::vec3& from, const glm::vec3& to,
                          const btCollisionObject* exclude = nullptr) const;

    btDiscreteDynamicsWorld* GetWorld() { return m_world.get(); }

private:
    std::unique_ptr<btDefaultCollisionConfiguration>     m_config;
    std::unique_ptr<btCollisionDispatcher>               m_dispatcher;
    std::unique_ptr<btDbvtBroadphase>                    m_broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver> m_solver;
    std::unique_ptr<btDiscreteDynamicsWorld>             m_world;

    // ownership storage
    std::vector<std::unique_ptr<btCollisionShape>>  m_shapes;
    std::vector<std::unique_ptr<btMotionState>>     m_motionStates;
    std::vector<std::unique_ptr<btRigidBody>>       m_bodies;

    btRigidBody* AddBody(btCollisionShape* shape, float mass,
                          const glm::vec3& pos);
};
