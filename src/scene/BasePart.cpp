#include "BasePart.h"
#include "physics/PhysicsUtils.h"
#include <btBulletDynamicsCommon.h>
#include <glm/gtc/matrix_transform.hpp>

glm::mat4 BasePart::GetModelMatrix() const {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m = m * glm::mat4_cast(rotation);
    m = glm::scale(m, size);
    return m;
}

void BasePart::SyncFromPhysics() {
    if (!rigidBody) return;

    btTransform t;
    rigidBody->getMotionState()->getWorldTransform(t);

    position = BtToGlm(t.getOrigin());
    rotation = BtToGlm(t.getRotation());
}
