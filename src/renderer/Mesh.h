#pragma once
#include <glad/glad.h>

class Mesh {
public:
    void Init();
    void Draw() const;
    void Shutdown();

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    int    m_indexCount = 0;
};
