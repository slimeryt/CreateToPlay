#include "Shader.h"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>

std::string Shader::ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        printf("Shader: cannot open '%s'\n", path.c_str());
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint Shader::CompileStage(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        printf("Shader compile error:\n%s\n", log);
    }
    return s;
}

bool Shader::Load(const std::string& vertPath, const std::string& fragPath) {
    std::string vs = ReadFile(vertPath);
    std::string fs = ReadFile(fragPath);
    if (vs.empty() || fs.empty()) return false;

    GLuint v = CompileStage(GL_VERTEX_SHADER,   vs);
    GLuint f = CompileStage(GL_FRAGMENT_SHADER, fs);

    m_id = glCreateProgram();
    glAttachShader(m_id, v);
    glAttachShader(m_id, f);
    glLinkProgram(m_id);

    GLint ok;
    glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_id, sizeof(log), nullptr, log);
        printf("Shader link error:\n%s\n", log);
        glDeleteShader(v);
        glDeleteShader(f);
        return false;
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return true;
}

void Shader::Use() const { glUseProgram(m_id); }
void Shader::Shutdown()  { if (m_id) { glDeleteProgram(m_id); m_id = 0; } }

void Shader::SetMat4(const char* name, const glm::mat4& v) const {
    glUniformMatrix4fv(glGetUniformLocation(m_id, name), 1, GL_FALSE, glm::value_ptr(v));
}
void Shader::SetMat3(const char* name, const glm::mat3& v) const {
    glUniformMatrix3fv(glGetUniformLocation(m_id, name), 1, GL_FALSE, glm::value_ptr(v));
}
void Shader::SetVec3(const char* name, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(m_id, name), 1, glm::value_ptr(v));
}
void Shader::SetFloat(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(m_id, name), v);
}
void Shader::SetInt(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(m_id, name), v);
}
