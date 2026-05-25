#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

class Shader {
public:
    bool Load(const std::string& vertPath, const std::string& fragPath);
    bool LoadFromSource(const char* vertSrc, const char* fragSrc);
    void Use() const;
    void Shutdown();

    void SetMat4(const char* name, const glm::mat4& v) const;
    void SetMat3(const char* name, const glm::mat3& v) const;
    void SetVec3(const char* name, const glm::vec3& v) const;
    void SetFloat(const char* name, float v)        const;
    void SetInt  (const char* name, int   v)        const;

private:
    GLuint m_id = 0;

    static GLuint CompileStage(GLenum type, const char* src);
    static std::string ReadFile(const std::string& path);
};
