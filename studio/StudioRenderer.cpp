#include "StudioRenderer.h"
#include "StudioCamera.h"
#include "EditorScene.h"
#include "embedded/EmbeddedAssets.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

// ── Flat-colour shader (grid) ─────────────────────────────────────────────────
static const char* kFlatVert = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)GLSL";

static const char* kFlatFrag = R"GLSL(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() { FragColor = vec4(uColor, 1.0); }
)GLSL";

// ── Lighting constants ────────────────────────────────────────────────────────
static const glm::vec3 kLightDir    = glm::normalize(glm::vec3( 0.8f,  1.6f,  0.5f));
static const glm::vec3 kLightColor  = glm::vec3(1.00f, 0.95f, 0.84f);
static const glm::vec3 kSkyColor    = glm::vec3(0.28f, 0.44f, 0.62f);
static const glm::vec3 kGroundColor = glm::vec3(0.14f, 0.11f, 0.08f);
static const glm::vec3 kFillDir     = glm::normalize(glm::vec3(-0.7f,  0.3f, -0.5f));
static const glm::vec3 kFillColor   = glm::vec3(0.10f, 0.20f, 0.38f);
static const glm::vec3 kFogColor    = glm::vec3(0.53f, 0.62f, 0.78f);
static constexpr float kFogDensity  = 0.004f;

// ─────────────────────────────────────────────────────────────────────────────
bool StudioRenderer::Init(int w, int h) {
    m_w = std::max(w, 1);
    m_h = std::max(h, 1);

    if (!m_phong.LoadFromSource(kPhongVert, kPhongFrag)) return false;
    if (!m_flat.LoadFromSource(kFlatVert,  kFlatFrag))  return false;

    m_cube.Init();
    CreateFBO();
    BuildGrid();

    // 1×1 dummy shadow texture (no real shadow pass in Studio).
    glGenTextures(1, &m_shadowTex);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 1, 1, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glm::mat4 lProj = glm::ortho(-60.f, 60.f, -60.f, 60.f, 1.f, 280.f);
    glm::mat4 lView = glm::lookAt(kLightDir * 120.f,
                                  glm::vec3(0.f),
                                  glm::vec3(0.f, 1.f, 0.f));
    m_lightSpace = lProj * lView;
    return true;
}

void StudioRenderer::Shutdown() {
    DestroyFBO();
    m_phong.Shutdown();
    m_flat.Shutdown();
    m_cube.Shutdown();
    if (m_gridVAO)  { glDeleteVertexArrays(1, &m_gridVAO); m_gridVAO  = 0; }
    if (m_gridVBO)  { glDeleteBuffers(1, &m_gridVBO);      m_gridVBO  = 0; }
    if (m_shadowTex){ glDeleteTextures(1, &m_shadowTex);   m_shadowTex= 0; }
}

void StudioRenderer::Resize(int w, int h) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (w == m_w && h == m_h) return;
    m_w = w; m_h = h;
    DestroyFBO();
    CreateFBO();
}

// ── FBO ───────────────────────────────────────────────────────────────────────
void StudioRenderer::CreateFBO() {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, m_w, m_h,
                 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_colorTex, 0);

    glGenRenderbuffers(1, &m_depthRB);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRB);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_w, m_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, m_depthRB);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void StudioRenderer::DestroyFBO() {
    if (m_fbo)     { glDeleteFramebuffers(1,  &m_fbo);     m_fbo     = 0; }
    if (m_colorTex){ glDeleteTextures(1,      &m_colorTex); m_colorTex= 0; }
    if (m_depthRB) { glDeleteRenderbuffers(1, &m_depthRB); m_depthRB = 0; }
}

// ── Grid ──────────────────────────────────────────────────────────────────────
void StudioRenderer::BuildGrid() {
    constexpr int   kHalf = 50;
    constexpr float kStep = 1.f;
    constexpr float kY    = 0.f;
    std::vector<float> v;
    v.reserve((kHalf * 2 + 1) * 4 * 3);
    for (int i = -kHalf; i <= kHalf; ++i) {
        float f = (float)i * kStep;
        // parallel to Z
        v.push_back(-kHalf * kStep); v.push_back(kY); v.push_back(f);
        v.push_back( kHalf * kStep); v.push_back(kY); v.push_back(f);
        // parallel to X
        v.push_back(f); v.push_back(kY); v.push_back(-kHalf * kStep);
        v.push_back(f); v.push_back(kY); v.push_back( kHalf * kStep);
    }
    m_gridVerts = (int)(v.size() / 3);

    glGenVertexArrays(1, &m_gridVAO);
    glGenBuffers(1, &m_gridVBO);
    glBindVertexArray(m_gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(v.size() * sizeof(float)),
                 v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// ── Frame API ─────────────────────────────────────────────────────────────────
void StudioRenderer::BeginFrame(const StudioCamera& camera) {
    m_view = camera.GetView();
    m_proj = camera.GetProjection();

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_w, m_h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0.53f, 0.62f, 0.78f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_phong.Use();
    m_phong.SetMat4("uView",             m_view);
    m_phong.SetMat4("uProjection",       m_proj);
    m_phong.SetVec3("uViewPos",          camera.GetPosition());
    m_phong.SetMat4("uLightSpaceMatrix", m_lightSpace);
    m_phong.SetVec3("uLightDir",         kLightDir);
    m_phong.SetVec3("uLightColor",       kLightColor);
    m_phong.SetVec3("uSkyColor",         kSkyColor);
    m_phong.SetVec3("uGroundColor",      kGroundColor);
    m_phong.SetVec3("uFillDir",          kFillDir);
    m_phong.SetVec3("uFillColor",        kFillColor);
    m_phong.SetVec3("uFogColor",         kFogColor);
    m_phong.SetFloat("uFogDensity",      kFogDensity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);
    m_phong.SetInt("uShadowMap", 0);

    m_inFrame = true;
}

void StudioRenderer::DrawPartNode(const EditorNode& part, bool selected) {
    if (part.type != EditorNodeType::Part) return;

    // Build the full TRS world matrix — this is what sizes the mesh.
    const glm::mat4 model = part.WorldMatrix();
    const glm::mat3 nm    = glm::transpose(glm::inverse(glm::mat3(model)));

    glm::vec3 col = part.color;
    if (selected)
        col = glm::min(col * 1.25f + glm::vec3(0.08f), glm::vec3(1.f));

    m_phong.SetMat4("uModel",        model);
    m_phong.SetMat3("uNormalMatrix", nm);
    m_phong.SetVec3("uObjectColor",  col);
    m_phong.SetFloat("uReflectance", part.reflectance);
    m_cube.Draw();
}

void StudioRenderer::DrawScene(const EditorScene& scene,
                                const EditorNode* selected) {
    // Make sure the phong shader is active (callers may have switched shaders).
    m_phong.Use();
    scene.ForEachPart([&](const EditorNode& p) {
        DrawPartNode(p, selected && selected == &p);
    });
}

void StudioRenderer::DrawGrid() {
    if (m_gridVerts <= 0) return;
    m_flat.Use();
    m_flat.SetVec3("uColor", glm::vec3(0.35f, 0.38f, 0.42f));
    m_flat.SetMat4("uMVP", m_proj * m_view);
    glLineWidth(1.f);
    glBindVertexArray(m_gridVAO);
    glDrawArrays(GL_LINES, 0, m_gridVerts);
    glBindVertexArray(0);
}

void StudioRenderer::EndFrame() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_inFrame = false;
}

// ── Convenience one-shot render ───────────────────────────────────────────────
void StudioRenderer::RenderAll(const EditorScene& scene,
                                const StudioCamera& camera) {
    BeginFrame(camera);
    DrawScene(scene, scene.GetSelected());
    DrawGrid();
    EndFrame();
}
