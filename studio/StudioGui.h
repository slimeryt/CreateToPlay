#pragma once
#include <SDL.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class StudioCamera;
class StudioRenderer;
class EditorScene;
class EditorNode;

// Shared data for the home-page game library.
struct GameEntry {
    std::string name;
    std::string lastEdited;
    glm::vec3   thumbnailColor{ 0.3f, 0.5f, 0.8f };
};

class StudioGui {
public:
    bool Init(SDL_Window* window, SDL_GLContext glContext);
    void Shutdown();

    void ProcessEvent(const SDL_Event& e);
    void BeginFrame();
    // ── Launcher screens ──────────────────────────────────────────────────────
    // Returns true the frame the user clicks Log In / Continue as Guest.
    // outUsername is set to the typed name, or "Guest".
    bool DrawLoginScreen(std::string& outUsername);

    // Returns 0 = stay, 1 = create new place, ≥2 = open games[ret-2].
    int  DrawHomeScreen(const std::string& username,
                        const std::vector<GameEntry>& games);

    // ── Studio editor ─────────────────────────────────────────────────────────
    // Returns true the frame the user clicks the Home button (go back to Home screen).
    bool DrawRibbon(EditorScene& scene);
    void DrawDockedPanels(EditorScene& scene, StudioCamera& camera, StudioRenderer& renderer);
    void EndFrame();

    bool IsScriptEditorOpen() const { return m_scriptEditorOpen; }

    bool ViewportHovered() const { return m_viewportHovered; }
    bool ViewportFocused() const { return m_viewportFocused; }

private:
    void ApplyStudioTheme();
    void DrawExplorer(EditorScene& scene);
    void DrawProperties(EditorScene& scene);
    void DrawViewport(EditorScene& scene, StudioCamera& camera, StudioRenderer& renderer);
    void DrawScriptEditor(EditorScene& scene);
    void DrawExplorerNode(EditorScene& scene, EditorNode& node);
    void HandleViewportPick(EditorScene& scene, StudioCamera& camera,
                            const ImVec2& vpPos, const ImVec2& vpSize);

    ImGuizmo::OPERATION m_gizmoOp   = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      m_gizmoMode = ImGuizmo::LOCAL;

    bool m_viewportHovered  = false;
    bool m_viewportFocused  = false;
    bool m_gizmoWasUsing       = false;
    bool m_gizmoDrawnThisFrame = false;
    bool      m_scaleGizmoActive = false;
    int       m_gizmoScaleSign   = 1;
    int       m_gizmoScaleAxis   = 0;
    glm::vec3 m_gizmoScaleStartCenter{ 0.f };
    glm::vec3 m_gizmoScaleStartSize{ 1.f };
    glm::vec3 m_gizmoScaleAxisWorld{ 1.f, 0.f, 0.f };
    // Screen-space tracking (avoids ImGuizmo matrix output for scale).
    glm::vec2 m_gizmoScaleMouseStart{ 0.f };   // px position at drag start
    glm::vec2 m_gizmoScaleAxDirPx{ 1.f, 0.f }; // normalised screen-space axis dir

    bool      m_axisDragActive     = false;
    bool      m_gizmoSkipApply     = false;
    glm::vec3 m_gizmoDragOrigin{ 0.f };
    glm::vec3 m_gizmoDragStartCenter{ 0.f };

    // ── Login state ───────────────────────────────────────────────────────────
    char m_loginUsername[64] = {};
    char m_loginPassword[64] = {};

    // ── Home state ────────────────────────────────────────────────────────────
    int  m_homeNav         = 0;          // 0 = Home, 1 = My Games
    char m_newPlaceName[128] = "My Place";
    bool m_newPlaceOpen    = false;

    // ── Explorer rename ───────────────────────────────────────────────────────
    bool m_renameOpen       = false;
    char m_renameBuf[128]   = {};

    ImFont* m_fontTitle = nullptr;
    ImFont* m_fontMono  = nullptr;

    // ── Script editor ─────────────────────────────────────────────────────────
    bool        m_scriptEditorOpen = false;
    EditorNode* m_editingScript    = nullptr;
};
