#include "Mesh.h"

// Unit cube: positions span -0.5..+0.5, 24 vertices (4 per face), 36 indices.
// layout: vec3 pos, vec3 normal, vec2 uv  (stride 8 floats)
static const float kVerts[] = {
    // +Z face (front)
    -0.5f,-0.5f, 0.5f,  0, 0, 1,  0,0,
     0.5f,-0.5f, 0.5f,  0, 0, 1,  1,0,
     0.5f, 0.5f, 0.5f,  0, 0, 1,  1,1,
    -0.5f, 0.5f, 0.5f,  0, 0, 1,  0,1,
    // -Z face (back)
     0.5f,-0.5f,-0.5f,  0, 0,-1,  0,0,
    -0.5f,-0.5f,-0.5f,  0, 0,-1,  1,0,
    -0.5f, 0.5f,-0.5f,  0, 0,-1,  1,1,
     0.5f, 0.5f,-0.5f,  0, 0,-1,  0,1,
    // +X face (right)
     0.5f,-0.5f, 0.5f,  1, 0, 0,  0,0,
     0.5f,-0.5f,-0.5f,  1, 0, 0,  1,0,
     0.5f, 0.5f,-0.5f,  1, 0, 0,  1,1,
     0.5f, 0.5f, 0.5f,  1, 0, 0,  0,1,
    // -X face (left)
    -0.5f,-0.5f,-0.5f, -1, 0, 0,  0,0,
    -0.5f,-0.5f, 0.5f, -1, 0, 0,  1,0,
    -0.5f, 0.5f, 0.5f, -1, 0, 0,  1,1,
    -0.5f, 0.5f,-0.5f, -1, 0, 0,  0,1,
    // +Y face (top)
    -0.5f, 0.5f, 0.5f,  0, 1, 0,  0,0,
     0.5f, 0.5f, 0.5f,  0, 1, 0,  1,0,
     0.5f, 0.5f,-0.5f,  0, 1, 0,  1,1,
    -0.5f, 0.5f,-0.5f,  0, 1, 0,  0,1,
    // -Y face (bottom)
    -0.5f,-0.5f,-0.5f,  0,-1, 0,  0,0,
     0.5f,-0.5f,-0.5f,  0,-1, 0,  1,0,
     0.5f,-0.5f, 0.5f,  0,-1, 0,  1,1,
    -0.5f,-0.5f, 0.5f,  0,-1, 0,  0,1,
};

static const unsigned int kIndices[] = {
     0, 1, 2,  2, 3, 0,   // front
     4, 5, 6,  6, 7, 4,   // back
     8, 9,10, 10,11, 8,   // right
    12,13,14, 14,15,12,   // left
    16,17,18, 18,19,16,   // top
    20,21,22, 22,23,20,   // bottom
};

void Mesh::Init() {
    m_indexCount = 36;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVerts), kVerts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

    const GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Mesh::Draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::Shutdown() {
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
    glDeleteVertexArrays(1, &m_vao);
}
