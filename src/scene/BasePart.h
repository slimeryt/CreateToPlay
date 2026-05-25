#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

class btRigidBody;

class BasePart {
public:
    std::string name;
    glm::vec3   position    = glm::vec3(0.0f);
    glm::quat   rotation    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3   size        = glm::vec3(1.0f);
    glm::vec3   color       = glm::vec3(0.6f);
    float       reflectance = 0.1f;

    btRigidBody* rigidBody = nullptr;

    glm::mat4 GetModelMatrix() const;
    void SyncFromPhysics();
};
