#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <btBulletDynamicsCommon.h>

// btVector3 ↔ glm::vec3
inline glm::vec3 BtToGlm(const btVector3& v) {
    return glm::vec3(v.x(), v.y(), v.z());
}
inline btVector3 GlmToBt(const glm::vec3& v) {
    return btVector3(v.x, v.y, v.z);
}

// btQuaternion(x,y,z,w) ↔ glm::quat(w,x,y,z)
inline glm::quat BtToGlm(const btQuaternion& q) {
    return glm::quat(q.w(), q.x(), q.y(), q.z());
}
inline btQuaternion GlmToBt(const glm::quat& q) {
    return btQuaternion(q.x, q.y, q.z, q.w);
}
