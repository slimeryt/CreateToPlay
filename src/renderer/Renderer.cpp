#include "Renderer.h"
#include "scene/BasePart.h"
#include "camera/Camera.h"
#include "embedded/EmbeddedAssets.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// ── Lighting constants ────────────────────────────────────────────────────────
static const glm::vec3 kLightDir    = glm::normalize(glm::vec3(0.8f, 1.6f, 0.5f));
static const glm::vec3 kLightColor  = glm::vec3(1.00f, 0.95f, 0.84f);
static const glm::vec3 kSkyColor    = glm::vec3(0.28f, 0.44f, 0.62f);
static const glm::vec3 kGroundColor = glm::vec3(0.14f, 0.11f, 0.08f);
static const glm::vec3 kFillDir     = glm::normalize(glm::vec3(-0.7f, 0.3f, -0.5f));
static const glm::vec3 kFillColor   = glm::vec3(0.10f, 0.20f, 0.38f);
static const glm::vec3 kFogColor    = glm::vec3(0.53f, 0.81f, 0.98f);
static constexpr float kFogDensity  = 0.006f;

// ── Init / Shutdown ───────────────────────────────────────────────────────────
bool Renderer::Init() {
    if (!m_phongShader.LoadFromSource(       kPhongVert,  kPhongFrag))         return false;
    if (!m_shadowShader.LoadFromSource(      kShadowVert, kShadowFrag))        return false;
    if (!m_skyShader.LoadFromSource(         kQuadVert,   kSkyFrag))           return false;
    if (!m_bloomExtractShader.LoadFromSource(kQuadVert,   kBloomExtractFrag))  return false;
    if (!m_blurShader.LoadFromSource(        kQuadVert,   kBlurFrag))          return false;
    if (!m_compositeShader.LoadFromSource(   kQuadVert,   kCompositeFrag))     return false;

    m_cube.Init();
    CreateShadowMap();
    CreateHDRBuffers();
    CreateBloomBuffers();
    CreateQuad();

    // Light-space matrix for shadow mapping (orthographic sun projection)
    glm::mat4 lightProj = glm::ortho(-60.f, 60.f, -60.f, 60.f, 1.f, 280.f);
    glm::mat4 lightView = glm::lookAt(kLightDir * 120.f, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    m_lightSpaceMatrix  = lightProj * lightView;

    return true;
}

void Renderer::Shutdown() {
    m_phongShader.Shutdown();
    m_shadowShader.Shutdown();
    m_skyShader.Shutdown();
    m_bloomExtractShader.Shutdown();
    m_blurShader.Shutdown();
    m_compositeShader.Shutdown();
    m_cube.Shutdown();

    DestroyHDRBuffers();
    DestroyBloomBuffers();

    if (m_shadowFBO) { glDeleteFramebuffers(1,  &m_shadowFBO); m_shadowFBO = 0; }
    if (m_shadowMap) { glDeleteTextures(1,       &m_shadowMap); m_shadowMap = 0; }
    if (m_quadVAO)   { glDeleteVertexArrays(1,   &m_quadVAO);   m_quadVAO  = 0; }
    if (m_quadVBO)   { glDeleteBuffers(1,         &m_quadVBO);   m_quadVBO  = 0; }
}

void Renderer::Resize(int w, int h) {
    if (w == m_width && h == m_height) return;
    m_width = w; m_height = h;
    DestroyHDRBuffers();
    DestroyBloomBuffers();
    CreateHDRBuffers();
    CreateBloomBuffers();
}

// ── Shadow map ────────────────────────────────────────────────────────────────
void Renderer::CreateShadowMap() {
    glGenTextures(1, &m_shadowMap);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                 kShadowRes, kShadowRes, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[] = {1.f, 1.f, 1.f, 1.f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glGenFramebuffers(1, &m_shadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ── HDR framebuffer ───────────────────────────────────────────────────────────
void Renderer::CreateHDRBuffers() {
    glGenFramebuffers(1, &m_hdrFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFBO);

    glGenTextures(1, &m_hdrColor);
    glBindTexture(GL_TEXTURE_2D, m_hdrColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, m_width, m_height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hdrColor, 0);

    glGenRenderbuffers(1, &m_hdrDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_hdrDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_hdrDepth);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::DestroyHDRBuffers() {
    if (m_hdrFBO)   { glDeleteFramebuffers(1,   &m_hdrFBO);   m_hdrFBO   = 0; }
    if (m_hdrColor) { glDeleteTextures(1,         &m_hdrColor); m_hdrColor = 0; }
    if (m_hdrDepth) { glDeleteRenderbuffers(1,    &m_hdrDepth); m_hdrDepth = 0; }
}

// ── Bloom FBOs ────────────────────────────────────────────────────────────────
void Renderer::CreateBloomBuffers() {
    auto makeTex = [&](GLuint& tex, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };

    // Extract FBO
    glGenFramebuffers(1, &m_bloomExtractFBO);
    makeTex(m_bloomExtractTex, m_width, m_height);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomExtractFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomExtractTex, 0);

    // Ping-pong FBOs
    glGenFramebuffers(2, m_bloomFBO);
    for (int i = 0; i < 2; ++i) {
        makeTex(m_bloomTex[i], m_width, m_height);
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomTex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::DestroyBloomBuffers() {
    if (m_bloomExtractFBO) { glDeleteFramebuffers(1, &m_bloomExtractFBO); m_bloomExtractFBO = 0; }
    if (m_bloomExtractTex) { glDeleteTextures(1,     &m_bloomExtractTex); m_bloomExtractTex = 0; }
    if (m_bloomFBO[0]) { glDeleteFramebuffers(2, m_bloomFBO); m_bloomFBO[0] = m_bloomFBO[1] = 0; }
    if (m_bloomTex[0]) { glDeleteTextures(2,     m_bloomTex); m_bloomTex[0] = m_bloomTex[1] = 0; }
}

// ── Screen quad ───────────────────────────────────────────────────────────────
void Renderer::CreateQuad() {
    static const float verts[] = {
        -1.f,-1.f,  0.f,0.f,   1.f,-1.f,  1.f,0.f,   1.f, 1.f,  1.f,1.f,
        -1.f,-1.f,  0.f,0.f,   1.f, 1.f,  1.f,1.f,  -1.f, 1.f,  0.f,1.f,
    };
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void Renderer::RenderQuad() {
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ── Render passes ─────────────────────────────────────────────────────────────
void Renderer::BeginShadowPass() {
    m_inShadow = true;
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);
    glViewport(0, 0, kShadowRes, kShadowRes);
    glClear(GL_DEPTH_BUFFER_BIT);
    glCullFace(GL_BACK);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    m_shadowShader.Use();
    m_shadowShader.SetMat4("uLightSpaceMatrix", m_lightSpaceMatrix);
}

void Renderer::DrawSky(const Camera& camera) {
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_skyShader.Use();
    m_skyShader.SetMat4("uInvProj", glm::inverse(camera.GetProjection()));
    m_skyShader.SetMat4("uInvView", glm::inverse(camera.GetView()));
    m_skyShader.SetVec3("uSunDir",  kLightDir);
    RenderQuad();

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void Renderer::BeginMainPass(const Camera& camera) {
    m_inShadow = false;
    glCullFace(GL_BACK);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFBO);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.f);  // black — sky covers it
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw procedural sky first (behind all geometry)
    DrawSky(camera);

    m_phongShader.Use();
    m_phongShader.SetMat4("uView",             camera.GetView());
    m_phongShader.SetMat4("uProjection",       camera.GetProjection());
    m_phongShader.SetVec3("uViewPos",          camera.GetPosition());
    m_phongShader.SetMat4("uLightSpaceMatrix", m_lightSpaceMatrix);
    m_phongShader.SetVec3("uLightDir",         kLightDir);
    m_phongShader.SetVec3("uLightColor",       kLightColor);
    m_phongShader.SetVec3("uSkyColor",         kSkyColor);
    m_phongShader.SetVec3("uGroundColor",      kGroundColor);
    m_phongShader.SetVec3("uFillDir",          kFillDir);
    m_phongShader.SetVec3("uFillColor",        kFillColor);
    m_phongShader.SetVec3("uFogColor",         kFogColor);
    m_phongShader.SetFloat("uFogDensity",      kFogDensity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    m_phongShader.SetInt("uShadowMap", 0);
}

void Renderer::DrawPart(const BasePart& part) {
    glm::mat4 model = part.GetModelMatrix();
    if (m_inShadow) {
        m_shadowShader.SetMat4("uModel", model);
    } else {
        glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(model)));
        m_phongShader.SetMat4("uModel",        model);
        m_phongShader.SetMat3("uNormalMatrix", nm);
        m_phongShader.SetVec3("uObjectColor",  part.color);
        m_phongShader.SetFloat("uReflectance", part.reflectance);
    }
    m_cube.Draw();
}

void Renderer::DrawBox(glm::vec3 pos, glm::vec3 size, glm::vec3 color,
                       float yaw, float reflectance) {
    glm::mat4 model = glm::translate(glm::mat4(1.f), pos);
    model = glm::rotate(model, yaw, glm::vec3(0.f, 1.f, 0.f));
    model = glm::scale(model, size);

    if (m_inShadow) {
        m_shadowShader.SetMat4("uModel", model);
    } else {
        glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(model)));
        m_phongShader.SetMat4("uModel",        model);
        m_phongShader.SetMat3("uNormalMatrix", nm);
        m_phongShader.SetVec3("uObjectColor",  color);
        m_phongShader.SetFloat("uReflectance", reflectance);
    }
    m_cube.Draw();
}

void Renderer::DrawBox(glm::vec3 pos, glm::vec3 size, glm::vec3 color,
                       const glm::quat& rotation, float reflectance) {
    glm::mat4 model = glm::translate(glm::mat4(1.f), pos);
    model = model * glm::mat4_cast(rotation);
    model = glm::scale(model, size);

    if (m_inShadow) {
        m_shadowShader.SetMat4("uModel", model);
    } else {
        glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(model)));
        m_phongShader.SetMat4("uModel",        model);
        m_phongShader.SetMat3("uNormalMatrix", nm);
        m_phongShader.SetVec3("uObjectColor",  color);
        m_phongShader.SetFloat("uReflectance", reflectance);
    }
    m_cube.Draw();
}

void Renderer::EndFrame() {
    glDisable(GL_DEPTH_TEST);

    // ── Bloom extract ──────────────────────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomExtractFBO);
    glViewport(0, 0, m_width, m_height);
    m_bloomExtractShader.Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColor);
    m_bloomExtractShader.SetInt("uScene", 0);
    m_bloomExtractShader.SetFloat("uThreshold", 0.70f);
    RenderQuad();

    // ── Gaussian blur: 10 ping-pong passes (5 H + 5 V) ────────────────────────
    m_blurShader.Use();
    int last = -1; // -1 = read from extract tex
    for (int i = 0; i < 10; ++i) {
        int target = i % 2;
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[target]);
        m_blurShader.SetInt("uHorizontal", (i % 2 == 0) ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,
                      last < 0 ? m_bloomExtractTex : m_bloomTex[1 - target]);
        m_blurShader.SetInt("uImage", 0);
        RenderQuad();
        last = target;
    }

    // ── Composite → default FBO ───────────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT);

    m_compositeShader.Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColor);
    m_compositeShader.SetInt("uHDRScene", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_bloomTex[last < 0 ? 0 : last]);
    m_compositeShader.SetInt("uBloom", 1);
    m_compositeShader.SetFloat("uExposure",      1.0f);
    m_compositeShader.SetFloat("uBloomStrength", 0.35f);
    RenderQuad();

    glEnable(GL_DEPTH_TEST);
}
