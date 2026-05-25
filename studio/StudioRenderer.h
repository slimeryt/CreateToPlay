#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "renderer/Shader.h"
#include "renderer/Mesh.h"

class StudioCamera;
class EditorScene;
struct EditorNode;

class StudioRenderer {
public:
    bool Init(int w, int h);
    void Shutdown();
    void Resize(int w, int h);

    // Render everything to the FBO in one call.
    // Call this after ALL ImGui panels have been processed so every
    // scene change from the current frame is captured.
    void RenderAll(const EditorScene& scene, const StudioCamera& camera);

    // Fine-grained API (used by DrawViewport for gizmo re-draws).
    void BeginFrame(const StudioCamera& camera);
    void DrawScene(const EditorScene& scene, const EditorNode* selected);
    void DrawGrid();
    void EndFrame();

    GLuint GetColorTexture() const { return m_colorTex; }
    int    Width()           const { return m_w; }
    int    Height()          const { return m_h; }

private:
    void CreateFBO();
    void DestroyFBO();
    void BuildGrid();
    void DrawPartNode(const EditorNode& part, bool selected);

    Shader m_phong;
    Shader m_flat;
    Mesh   m_cube;

    GLuint m_fbo       = 0;
    GLuint m_colorTex  = 0;
    GLuint m_depthRB   = 0;
    GLuint m_shadowTex = 0;

    GLuint m_gridVAO   = 0;
    GLuint m_gridVBO   = 0;
    int    m_gridVerts = 0;

    int  m_w       = 1;
    int  m_h       = 1;
    bool m_inFrame = false;

    glm::mat4 m_lightSpace = glm::mat4(1.f);
    glm::mat4 m_view       = glm::mat4(1.f);
    glm::mat4 m_proj       = glm::mat4(1.f);
};
