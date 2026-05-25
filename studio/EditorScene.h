#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum class EditorNodeType {
    DataModel, Workspace, Service, Model, Part,
    Script, LocalScript, ModuleScript
};

struct EditorNode {
    EditorNodeType type   = EditorNodeType::Part;
    std::string    name;
    std::string    uniqueId;
    EditorNode*    parent = nullptr;
    std::vector<std::unique_ptr<EditorNode>> children;

    // Transform (local, relative to parent)
    glm::vec3 position  = { 0.f, 0.f, 0.f };
    glm::vec3 eulerDeg  = { 0.f, 0.f, 0.f };  // Yaw-Pitch-Roll applied Y→X→Z

    // Part properties
    glm::vec3 size        = { 4.f, 1.f, 4.f };
    glm::vec3 color       = { 0.639f, 0.635f, 0.647f };
    float     reflectance = 0.08f;
    bool      anchored    = true;

    // Script properties (Script / LocalScript / ModuleScript only)
    std::string source;
    bool        scriptEnabled = true;

    // Local matrix without scale (T*R).  Gizmo reads this.
    glm::mat4 LocalTRMatrix() const;
    // Full local matrix: T*R*S for Parts, T*R for everything else.
    glm::mat4 LocalMatrix()   const;
    // World matrices (chain to root).
    glm::mat4 WorldMatrix()   const;
    // World T*R only — no part scale — used to place the gizmo.
    glm::mat4 WorldTRMatrix() const;

    // Decompose a rotation matrix back to eulerDeg.
    void SetRotationFromMatrix(const glm::mat3& rot);
};

class EditorScene {
public:
    void CreateDefaultScene();

    EditorNode*       GetRoot()      { return m_root.get(); }
    const EditorNode* GetRoot()      const { return m_root.get(); }
    EditorNode*       GetWorkspace();
    const EditorNode* GetWorkspace() const;
    EditorNode*       GetSelected() { return m_selected; }
    const EditorNode* GetSelected() const { return m_selected; }
    void              SetSelected(EditorNode* n) { m_selected = n; }

    EditorNode* InsertModel();
    EditorNode* InsertPart();
    EditorNode* InsertScript();
    EditorNode* InsertLocalScript();
    EditorNode* InsertModuleScript();
    bool        DeleteSelected();
    bool        ReparentSelected(EditorNode* newParent);

    EditorNode* FindNodeByName(const std::string& name,
                               EditorNode* start = nullptr);

    void ForEachPart(const std::function<void(EditorNode&)>&       fn);
    void ForEachPart(const std::function<void(const EditorNode&)>& fn) const;

    // ── Gizmo helpers ────────────────────────────────────────────────────────

    // World-space center of a Part (column 3 of its WorldMatrix).
    static glm::vec3 PartWorldCenter(const EditorNode& part);

    // Move a part so its world-space center equals worldCenter.
    void SetPartWorldCenter(EditorNode& part, const glm::vec3& worldCenter);

    // Translate + clamp-above-floor (used by gizmo translate).
    void ApplyGizmoTranslation(EditorNode& part, const glm::vec3& worldCenter);

    // One-sided scale along a world axis.
    //   axis      : 0/1/2 = X/Y/Z
    //   sign      : +1 = dragging positive end, -1 = negative end
    //   anchorFace: fixed face center in world space
    //   startSize : part size at drag start
    //   newAxisSize: desired new size along axis (clamped to >= 0.05)
    //   axisWorld : unit vector of axis in world space
    void ApplyGizmoScale(EditorNode& part, int axis, int sign,
                         const glm::vec3& anchorFace, const glm::vec3& startSize,
                         float newAxisSize, const glm::vec3& axisWorld);

    // Push a part out of overlaps with every other part (Roblox-style stacking).
    void ResolvePartCollisions(EditorNode& part);

private:
    std::unique_ptr<EditorNode> m_root;
    EditorNode*                 m_selected = nullptr;

    EditorNode* CreateNode(EditorNodeType type, const std::string& name,
                           EditorNode* parent);
    EditorNode* GetInsertParent() const;
    EditorNode* GetScriptInsertParent() const;
    void        RemoveNodeFromParent(EditorNode* node);
    std::string AllocUniqueId();

    // Y of the highest surface under (wx, wz), excluding `skip`.
    float FindFloorY(float wx, float wz, const EditorNode* skip) const;
};
