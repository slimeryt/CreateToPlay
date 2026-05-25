#include "EditorScene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

// ─────────────────────────────────────────────────────────────────────────────
// Internal AABB helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct AABB {
    glm::vec3 mn{ 0.f };
    glm::vec3 mx{ 0.f };

    bool Overlaps(const AABB& o) const {
        return mn.x < o.mx.x && mx.x > o.mn.x
            && mn.y < o.mx.y && mx.y > o.mn.y
            && mn.z < o.mx.z && mx.z > o.mn.z;
    }
    bool OverlapsXZ(const AABB& o) const {
        return mn.x < o.mx.x && mx.x > o.mn.x
            && mn.z < o.mx.z && mx.z > o.mn.z;
    }
    void Expand(const glm::vec3& p) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
};

constexpr float kSkin = 0.005f;

AABB PartAABB(const EditorNode& p) {
    const glm::mat4 wm   = p.WorldMatrix();
    const glm::vec3 half = p.size * 0.5f;
    AABB box;
    bool first = true;
    for (int sx : {-1, 1})
    for (int sy : {-1, 1})
    for (int sz : {-1, 1}) {
        glm::vec3 v = glm::vec3(wm * glm::vec4(
            half.x * sx, half.y * sy, half.z * sz, 1.f));
        if (first) { box.mn = box.mx = v; first = false; }
        else         box.Expand(v);
    }
    return box;
}

// Smallest separation delta to push AABB a out of AABB b.
// Strongly prefers pushing up (+Y) to simulate Roblox-style stacking.
glm::vec3 SepDelta(const AABB& a, const AABB& b) {
    if (!a.Overlaps(b)) return glm::vec3(0.f);

    struct Opt { glm::vec3 d; float cost; bool up; };
    Opt opts[6];
    int n = 0;
    auto add = [&](float depth, glm::vec3 dir, bool up) {
        if (depth > 1e-5f)
            opts[n++] = { dir * (depth + kSkin), depth + kSkin, up };
    };
    add(b.mx.x - a.mn.x, { 1,0,0}, false);
    add(a.mx.x - b.mn.x, {-1,0,0}, false);
    add(b.mx.y - a.mn.y, { 0,1,0}, true);
    add(a.mx.y - b.mn.y, { 0,-1,0}, false);
    add(b.mx.z - a.mn.z, { 0,0,1}, false);
    add(a.mx.z - b.mn.z, { 0,0,-1}, false);
    if (n == 0) return glm::vec3(0.f);

    // Prefer up unless a small sideways nudge is cheaper.
    float pushUp = b.mx.y - a.mn.y;
    if (pushUp > 1e-5f) {
        bool takeUp = true;
        for (int i = 0; i < n; ++i)
            if (!opts[i].up && opts[i].cost < pushUp * 0.5f) { takeUp = false; break; }
        if (takeUp) return glm::vec3(0.f, pushUp + kSkin, 0.f);
    }

    float     best  = std::numeric_limits<float>::max();
    glm::vec3 bestD = glm::vec3(0.f);
    for (int i = 0; i < n; ++i) {
        float c = opts[i].cost * (opts[i].up ? 0.5f : 1.f);
        if (c < best) { best = c; bestD = opts[i].d; }
    }
    return bestD;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// EditorNode matrix math
// ─────────────────────────────────────────────────────────────────────────────

// Local T*R (no scale).  GLM's glm::rotate(M, a, v) = M * Rot, so the chain
// T * Ry * Rx * Rz is built left-to-right with successive glm::rotate calls.
glm::mat4 EditorNode::LocalTRMatrix() const {
    glm::mat4 m = glm::translate(glm::mat4(1.f), position);
    m = glm::rotate(m, glm::radians(eulerDeg.y), glm::vec3(0,1,0));
    m = glm::rotate(m, glm::radians(eulerDeg.x), glm::vec3(1,0,0));
    m = glm::rotate(m, glm::radians(eulerDeg.z), glm::vec3(0,0,1));
    return m;
}

// Full local matrix: T*R*S for Parts, T*R for Models/Workspace.
glm::mat4 EditorNode::LocalMatrix() const {
    glm::mat4 m = LocalTRMatrix();
    if (type == EditorNodeType::Part)
        m = glm::scale(m, size);
    return m;
}

// World matrix: chain from root → this node.
glm::mat4 EditorNode::WorldMatrix() const {
    if (parent) return parent->WorldMatrix() * LocalMatrix();
    return LocalMatrix();
}

// World T*R only (no part scale) — for gizmo placement.
// Skip Workspace's/DataModel's transform (both are identity roots).
glm::mat4 EditorNode::WorldTRMatrix() const {
    glm::mat4 local = LocalTRMatrix();
    if (parent && parent->type != EditorNodeType::Workspace
               && parent->type != EditorNodeType::DataModel)
        return parent->WorldMatrix() * local;
    return local;
}

void EditorNode::SetRotationFromMatrix(const glm::mat3& rot) {
    // Normalise columns to remove any residual scale.
    glm::mat3 r = rot;
    for (int i = 0; i < 3; ++i) {
        float len = glm::length(r[i]);
        if (len > 1e-6f) r[i] /= len;
    }
    float yaw, pitch, roll;
    glm::extractEulerAngleYXZ(glm::mat4(r), yaw, pitch, roll);
    eulerDeg.y = glm::degrees(yaw);
    eulerDeg.x = glm::degrees(pitch);
    eulerDeg.z = glm::degrees(roll);
}

// ─────────────────────────────────────────────────────────────────────────────
// EditorScene – static helpers
// ─────────────────────────────────────────────────────────────────────────────

/*static*/
glm::vec3 EditorScene::PartWorldCenter(const EditorNode& part) {
    // Column 3 of WorldMatrix is the world-space position of the origin,
    // which for a TRS matrix equals the translation component — i.e. the
    // centre of the part.
    return glm::vec3(part.WorldMatrix()[3]);
}

void EditorScene::SetPartWorldCenter(EditorNode& part,
                                     const glm::vec3& worldCenter) {
    // We want:   T(pos) * R * S  applied inside parent, such that column 3
    // of WorldMatrix equals worldCenter.
    //
    // WorldMatrix = parentWorld * T(pos) * R * S
    // Column 3 of T(pos)*R*S  =  pos   (rotation/scale don't move column 3
    //                                    of a TRS matrix when the translation
    //                                    is in T(pos)).
    // Therefore:  parentWorld * pos_h = worldCenter_h
    //             pos = inv(parentWorld) * worldCenter
    glm::mat4 parentWorld(1.f);
    if (part.parent && part.parent->type != EditorNodeType::Workspace
                    && part.parent->type != EditorNodeType::DataModel)
        parentWorld = part.parent->WorldMatrix();

    part.position = glm::vec3(glm::inverse(parentWorld) *
                              glm::vec4(worldCenter, 1.f));
}

float EditorScene::FindFloorY(float wx, float wz,
                               const EditorNode* skip) const {
    float best = -std::numeric_limits<float>::max();
    bool  any  = false;
    ForEachPart([&](const EditorNode& p) {
        if (&p == skip) return;
        AABB b = PartAABB(p);
        if (wx >= b.mn.x && wx <= b.mx.x &&
            wz >= b.mn.z && wz <= b.mx.z) {
            best = std::max(best, b.mx.y);
            any  = true;
        }
    });
    return any ? best : -std::numeric_limits<float>::max();
}

// ─────────────────────────────────────────────────────────────────────────────
// Gizmo operations
// ─────────────────────────────────────────────────────────────────────────────

void EditorScene::ApplyGizmoTranslation(EditorNode& part,
                                        const glm::vec3& worldCenter) {
    if (part.type != EditorNodeType::Part) return;

    glm::vec3 wc = worldCenter;
    SetPartWorldCenter(part, wc);

    // Clamp so the bottom of the part cannot sink below any surface under it.
    const float floorY = FindFloorY(wc.x, wc.z, &part);
    if (floorY == -std::numeric_limits<float>::max()) return;

    for (int i = 0; i < 16; ++i) {
        AABB a = PartAABB(part);
        if (a.mn.y >= floorY + kSkin - 1e-5f) break;
        wc.y += (floorY + kSkin) - a.mn.y;
        SetPartWorldCenter(part, wc);
    }
}

void EditorScene::ApplyGizmoScale(EditorNode& part,
                                  int axis, int sign,
                                  const glm::vec3& anchorFace,
                                  const glm::vec3& startSize,
                                  float newAxisSize,
                                  const glm::vec3& axisWorld) {
    if (part.type != EditorNodeType::Part) return;
    if (axis < 0 || axis > 2 || sign == 0)  return;

    constexpr float kMin = 0.05f;
    newAxisSize = std::max(newAxisSize, kMin);

    // 1. Apply new size.
    part.size        = startSize;
    part.size[axis]  = newAxisSize;

    // 2. Keep the anchor face fixed.
    //    New centre = anchorFace + axisDirection * (newSize/2) * sign.
    //    (anchorFace is the face centre *opposite* to the dragged face.)
    const glm::vec3 newCenter =
        anchorFace + axisWorld * (newAxisSize * 0.5f) * (float)sign;
    SetPartWorldCenter(part, newCenter);
}

void EditorScene::ResolvePartCollisions(EditorNode& part) {
    if (part.type != EditorNodeType::Part) return;
    ApplyGizmoTranslation(part, PartWorldCenter(part));

    for (int pass = 0; pass < 16; ++pass) {
        AABB      a      = PartAABB(part);
        glm::vec3 bestD  = glm::vec3(0.f);
        float     bestL  = 0.f;

        ForEachPart([&](EditorNode& other) {
            if (&other == &part) return;
            glm::vec3 d = SepDelta(a, PartAABB(other));
            if (d.y < 0.f) d.y = 0.f; // never push down
            float len = glm::length(d);
            if (len > bestL) { bestL = len; bestD = d; }
        });

        if (bestL < 1e-5f) break;
        SetPartWorldCenter(part, PartWorldCenter(part) + bestD);
        ApplyGizmoTranslation(part, PartWorldCenter(part));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene management
// ─────────────────────────────────────────────────────────────────────────────

std::string EditorScene::AllocUniqueId() {
    static const char kHex[] = "0123456789abcdef";
    std::random_device              rd;
    std::mt19937                    gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::string id;
    id.reserve(17);
    for (int i = 0; i < 17; ++i) id += kHex[dis(gen)];
    return id;
}

EditorNode* EditorScene::CreateNode(EditorNodeType type,
                                    const std::string& name,
                                    EditorNode* parent) {
    auto n       = std::make_unique<EditorNode>();
    n->type      = type;
    n->name      = name;
    n->parent    = parent;
    if (type == EditorNodeType::Part) {
        n->uniqueId = AllocUniqueId();
        n->size     = { 4.f, 1.f, 4.f };
        n->position = { 0.f, 0.f, 0.f };
    } else if (type == EditorNodeType::Script) {
        n->source = "-- ServerScript: " + name + "\n\n";
    } else if (type == EditorNodeType::LocalScript) {
        n->source = "-- LocalScript: " + name + "\n\n";
    } else if (type == EditorNodeType::ModuleScript) {
        n->source = "local module = {}\n\n\nreturn module\n";
    }
    EditorNode* raw = n.get();
    parent->children.push_back(std::move(n));
    return raw;
}

EditorNode* EditorScene::GetWorkspace() {
    if (!m_root) return nullptr;
    for (auto& c : m_root->children)
        if (c->type == EditorNodeType::Workspace) return c.get();
    return nullptr;
}
const EditorNode* EditorScene::GetWorkspace() const {
    if (!m_root) return nullptr;
    for (const auto& c : m_root->children)
        if (c->type == EditorNodeType::Workspace) return c.get();
    return nullptr;
}

EditorNode* EditorScene::GetInsertParent() const {
    EditorNode* ws = const_cast<EditorScene*>(this)->GetWorkspace();
    EditorNode* fallback = ws ? ws : m_root.get();

    if (!m_selected) return fallback;
    if (m_selected->type == EditorNodeType::Workspace ||
        m_selected->type == EditorNodeType::Model)
        return m_selected;
    // Services and DataModel are not valid insertion targets
    if (m_selected->type == EditorNodeType::Service ||
        m_selected->type == EditorNodeType::DataModel)
        return fallback;
    return m_selected->parent ? m_selected->parent : fallback;
}

void EditorScene::CreateDefaultScene() {
    m_selected = nullptr;

    // DataModel root (the "game" object)
    m_root       = std::make_unique<EditorNode>();
    m_root->type = EditorNodeType::DataModel;
    m_root->name = "Game";

    // ── Workspace ─────────────────────────────────────────────────────────────
    EditorNode* ws = CreateNode(EditorNodeType::Workspace, "Workspace", m_root.get());

    auto addPart = [&](const std::string& name, glm::vec3 pos, glm::vec3 sz,
                       glm::vec3 col, EditorNode* parent) {
        EditorNode* n  = CreateNode(EditorNodeType::Part, name, parent);
        n->position    = pos;
        n->size        = sz;
        n->color       = col;
        n->reflectance = 0.08f;
    };

    addPart("Baseplate", {0.f, -0.2f, 0.f}, {100.f, 0.4f, 100.f},
            {0.38f, 0.56f, 0.29f}, ws);

    EditorNode* deco = CreateNode(EditorNodeType::Model, "Decorations", ws);
    addPart("RedBlock",   { 10.f, 1.f,  10.f}, {4.f, 2.f, 4.f},
            {0.80f, 0.22f, 0.22f}, deco);
    addPart("BlueBlock",  { -8.f, 1.f,  -6.f}, {3.f, 2.f, 3.f},
            {0.22f, 0.35f, 0.80f}, deco);
    addPart("YellowSlab", {  5.f, 0.5f,-12.f}, {6.f, 1.f, 2.f},
            {0.85f, 0.80f, 0.20f}, deco);

    // ── Services ──────────────────────────────────────────────────────────────
    static const char* kServices[] = {
        "ServerScriptService", "StarterPlayer", "StarterGui",
        "ReplicatedStorage",   "ServerStorage", "SoundService"
    };
    for (const char* svc : kServices)
        CreateNode(EditorNodeType::Service, svc, m_root.get());

    // Default selection: first non-baseplate part
    ForEachPart([&](EditorNode& p) {
        if (!m_selected && p.name != "Baseplate") m_selected = &p;
    });
}

EditorNode* EditorScene::InsertModel() {
    EditorNode* parent = GetInsertParent();
    std::string name   = "Model" + std::to_string(parent->children.size() + 1);
    EditorNode* n      = CreateNode(EditorNodeType::Model, name, parent);
    m_selected         = n;
    return n;
}

EditorNode* EditorScene::InsertPart() {
    EditorNode* parent = GetInsertParent();
    EditorNode* n      = CreateNode(EditorNodeType::Part, "Part", parent);

    // Snap to floor if one exists.
    glm::vec3 center = PartWorldCenter(*n);
    float     floor  = FindFloorY(center.x, center.z, n);
    if (floor != -std::numeric_limits<float>::max())
        center.y = floor + n->size.y * 0.5f;
    SetPartWorldCenter(*n, center);

    m_selected = n;
    return n;
}

// Scripts can live in Services, Workspace, or Models — but not in Parts.
EditorNode* EditorScene::GetScriptInsertParent() const {
    EditorNode* ws = const_cast<EditorScene*>(this)->GetWorkspace();
    EditorNode* fallback = ws ? ws : m_root.get();
    if (!m_selected) return fallback;
    auto t = m_selected->type;
    if (t == EditorNodeType::Workspace || t == EditorNodeType::Service ||
        t == EditorNodeType::Model)
        return m_selected;
    if (t == EditorNodeType::Script    || t == EditorNodeType::LocalScript ||
        t == EditorNodeType::ModuleScript)
        return m_selected->parent ? m_selected->parent : fallback;
    return m_selected->parent ? m_selected->parent : fallback;
}

EditorNode* EditorScene::InsertScript() {
    EditorNode* n = CreateNode(EditorNodeType::Script,
                               "Script", GetScriptInsertParent());
    m_selected = n;
    return n;
}

EditorNode* EditorScene::InsertLocalScript() {
    EditorNode* n = CreateNode(EditorNodeType::LocalScript,
                               "LocalScript", GetScriptInsertParent());
    m_selected = n;
    return n;
}

EditorNode* EditorScene::InsertModuleScript() {
    EditorNode* n = CreateNode(EditorNodeType::ModuleScript,
                               "ModuleScript", GetScriptInsertParent());
    m_selected = n;
    return n;
}

void EditorScene::RemoveNodeFromParent(EditorNode* node) {
    if (!node || !node->parent) return;
    auto& sib = node->parent->children;
    sib.erase(
        std::remove_if(sib.begin(), sib.end(),
            [&](const std::unique_ptr<EditorNode>& p) {
                return p.get() == node;
            }),
        sib.end());
}

bool EditorScene::DeleteSelected() {
    if (!m_selected || m_selected == m_root.get()) return false;
    // Protect structural nodes
    if (m_selected->type == EditorNodeType::DataModel  ||
        m_selected->type == EditorNodeType::Workspace  ||
        m_selected->type == EditorNodeType::Service)   return false;
    EditorNode* par = m_selected->parent;
    RemoveNodeFromParent(m_selected);
    m_selected = par ? par : m_root.get();
    return true;
}

bool EditorScene::ReparentSelected(EditorNode* newParent) {
    if (!m_selected || !newParent) return false;
    if (m_selected == m_root.get())                            return false;
    if (m_selected->type == EditorNodeType::DataModel  ||
        m_selected->type == EditorNodeType::Workspace  ||
        m_selected->type == EditorNodeType::Service)          return false;
    if (newParent->type  == EditorNodeType::Part      ||
        newParent->type  == EditorNodeType::DataModel  ||
        newParent->type  == EditorNodeType::Service)          return false;
    if (m_selected->parent == newParent)                      return false;

    EditorNode* node      = m_selected;
    EditorNode* oldParent = node->parent;
    if (!oldParent) return false;

    std::unique_ptr<EditorNode> owned;
    auto& sib = oldParent->children;
    for (auto it = sib.begin(); it != sib.end(); ++it) {
        if (it->get() == node) {
            owned = std::move(*it);
            sib.erase(it);
            break;
        }
    }
    if (!owned) return false;
    owned->parent = newParent;
    newParent->children.push_back(std::move(owned));
    return true;
}

EditorNode* EditorScene::FindNodeByName(const std::string& name,
                                        EditorNode* start) {
    if (!start) start = m_root.get();
    if (!start) return nullptr;
    if (start->name == name) return start;
    for (auto& c : start->children)
        if (auto* f = FindNodeByName(name, c.get())) return f;
    return nullptr;
}

void EditorScene::ForEachPart(
    const std::function<void(EditorNode&)>& fn) {
    if (!m_root) return;
    std::function<void(EditorNode&)> walk = [&](EditorNode& n) {
        if (n.type == EditorNodeType::Part) fn(n);
        for (auto& c : n.children) walk(*c);
    };
    walk(*m_root);
}

void EditorScene::ForEachPart(
    const std::function<void(const EditorNode&)>& fn) const {
    if (!m_root) return;
    std::function<void(const EditorNode&)> walk = [&](const EditorNode& n) {
        if (n.type == EditorNodeType::Part) fn(n);
        for (const auto& c : n.children) walk(*c);
    };
    walk(*m_root);
}
