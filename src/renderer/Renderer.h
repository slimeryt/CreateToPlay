#pragma once
#include "Shader.h"
#include "Mesh.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

class BasePart;
class Camera;

class Renderer {
public:
    bool Init();
    void Shutdown();
    void Resize(int w, int h);

    void BeginShadowPass();
    void BeginMainPass(const Camera& camera);
    void DrawPart(const BasePart& part);
    // Draw an arbitrary box without a BasePart (used for remote player ghosts)
    void DrawBox(glm::vec3 pos, glm::vec3 size, glm::vec3 color,
                 float yaw = 0.f, float reflectance = 0.04f);
    // Quaternion overload — used for animated limbs
    void DrawBox(glm::vec3 pos, glm::vec3 size, glm::vec3 color,
                 const glm::quat& rotation, float reflectance = 0.04f);
    void EndFrame();           // bloom + tonemap + gamma → default FBO

private:
    void CreateShadowMap();
    void CreateHDRBuffers();
    void CreateBloomBuffers();
    void DestroyHDRBuffers();
    void DestroyBloomBuffers();
    void CreateQuad();
    void RenderQuad();

    void DrawSky(const Camera& camera);

    Mesh   m_cube;
    Shader m_phongShader;
    Shader m_shadowShader;
    Shader m_skyShader;
    Shader m_bloomExtractShader;
    Shader m_blurShader;
    Shader m_compositeShader;

    // Shadow map (2048² depth texture, fixed size)
    GLuint    m_shadowFBO = 0;
    GLuint    m_shadowMap = 0;
    glm::mat4 m_lightSpaceMatrix{1.f};
    static constexpr int kShadowRes = 2048;

    // HDR render target (window-sized, GL_RGB16F)
    GLuint m_hdrFBO   = 0;
    GLuint m_hdrColor = 0;
    GLuint m_hdrDepth = 0;  // renderbuffer

    // Bloom: extract → ping-pong blur
    GLuint m_bloomExtractFBO = 0;
    GLuint m_bloomExtractTex = 0;
    GLuint m_bloomFBO[2]     = {};
    GLuint m_bloomTex[2]     = {};

    // Full-screen quad
    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;

    int  m_width    = 1280;
    int  m_height   = 720;
    bool m_inShadow = false;
};
