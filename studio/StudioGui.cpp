#include "StudioGui.h"
#include "EditorScene.h"
#include "StudioCamera.h"
#include "StudioRenderer.h"
#include "embedded/EmbeddedFont.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

static bool RayAABB(const glm::vec3& ro, const glm::vec3& rd,
                    const glm::vec3& bmin, const glm::vec3& bmax, float& tOut) {
    glm::vec3 inv = 1.f / rd;
    glm::vec3 t0  = (bmin - ro) * inv;
    glm::vec3 t1  = (bmax - ro) * inv;
    glm::vec3 tsm = glm::min(t0, t1);
    glm::vec3 tbg = glm::max(t0, t1);
    float tnear   = std::max({ tsm.x, tsm.y, tsm.z });
    float tfar    = std::min({ tbg.x, tbg.y, tbg.z });
    if (tfar < tnear || tfar < 0.f) return false;
    tOut = tnear > 0.f ? tnear : tfar;
    return true;
}

void StudioGui::ApplyStudioTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 6.f;
    s.FrameRounding     = 6.f;
    s.GrabRounding      = 6.f;
    s.ScrollbarRounding = 6.f;
    s.WindowBorderSize  = 1.f;

    const ImVec4 panel  = { 0.15f, 0.15f, 0.16f, 1.f };
    const ImVec4 border = { 0.28f, 0.28f, 0.30f, 1.f };

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = panel;
    c[ImGuiCol_ChildBg]           = panel;
    c[ImGuiCol_PopupBg]           = panel;
    c[ImGuiCol_MenuBarBg]         = panel;
    c[ImGuiCol_TitleBg]           = panel;
    c[ImGuiCol_TitleBgActive]     = panel;
    c[ImGuiCol_Tab]               = panel;
    c[ImGuiCol_TabActive]         = panel;
    c[ImGuiCol_TabHovered]        = panel;
    c[ImGuiCol_Button]            = panel;
    c[ImGuiCol_ButtonHovered]     = panel;
    c[ImGuiCol_ButtonActive]      = panel;
    c[ImGuiCol_FrameBg]           = panel;
    c[ImGuiCol_FrameBgHovered]    = panel;
    c[ImGuiCol_FrameBgActive]     = panel;
    c[ImGuiCol_TableHeaderBg]     = panel;
    c[ImGuiCol_TableRowBg]        = panel;
    c[ImGuiCol_TableRowBgAlt]     = panel;
    c[ImGuiCol_Border]            = border;
    c[ImGuiCol_Header]            = panel;
    c[ImGuiCol_HeaderHovered]     = panel;
    c[ImGuiCol_HeaderActive]      = panel;
    c[ImGuiCol_CheckMark]         = { 0.35f, 0.65f, 1.00f, 1.f };
    c[ImGuiCol_SliderGrab]        = { 0.35f, 0.65f, 1.00f, 1.f };
    c[ImGuiCol_Separator]         = border;
    c[ImGuiCol_TableBorderLight]  = border;
    c[ImGuiCol_TableBorderStrong] = border;
}

bool StudioGui::Init(SDL_Window* window, SDL_GLContext glContext) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImFontConfig cfg;
    cfg.OversampleH          = 3;
    cfg.OversampleV          = 3;
    cfg.PixelSnapH           = false;
    cfg.FontDataOwnedByAtlas = false;

    io.Fonts->AddFontFromMemoryTTF(
        (void*)kRobotoFontData, (int)kRobotoFontSize, 17.f, &cfg);
    m_fontTitle = io.Fonts->AddFontFromMemoryTTF(
        (void*)kRobotoFontData, (int)kRobotoFontSize, 22.f, &cfg);
    if (!m_fontTitle) m_fontTitle = io.Fonts->Fonts[0];

    // Monospace font for the script editor (Consolas → Courier New fallback)
    ImFontConfig monoCfg;
    monoCfg.OversampleH = 2; monoCfg.OversampleV = 2;
    m_fontMono = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\consola.ttf", 15.f, &monoCfg);
    if (!m_fontMono)
        m_fontMono = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\cour.ttf", 15.f, &monoCfg);
    if (!m_fontMono) m_fontMono = io.Fonts->Fonts[0];

    ImGui::GetStyle().ScaleAllSizes(1.15f);
    ApplyStudioTheme();

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    ImGuizmo::Style& gz = ImGuizmo::GetStyle();
    gz.TranslationLineThickness = 3.f;
    gz.TranslationLineArrowSize = 10.f;
    gz.ScaleLineCircleSize      = 14.f;
    gz.CenterCircleSize         = 0.f;
    ImGuizmo::SetGizmoSizeClipSpace(0.12f);

    return true;
}

static void ConfigureImGuizmoForOp(ImGuizmo::OPERATION op) {
    ImGuizmo::Style& gz = ImGuizmo::GetStyle();
    // mPlaneLimit gates axis arrows; mAxisLimit gates plane squares.
    ImGuizmo::SetPlaneLimit(0.02f);
    if (op == ImGuizmo::TRANSLATE) {
        ImGuizmo::SetAxisLimit(1e6f);
        gz.ScaleLineThickness = 3.f;
    } else if (op == ImGuizmo::SCALE) {
        ImGuizmo::SetAxisLimit(0.02f);
        gz.ScaleLineThickness = 2.f;
    } else {
        ImGuizmo::SetAxisLimit(0.02f);
        gz.ScaleLineThickness = 3.f;
    }
}

void StudioGui::Shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void StudioGui::ProcessEvent(const SDL_Event& e) {
    ImGui_ImplSDL2_ProcessEvent(&e);
}

void StudioGui::BeginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

bool StudioGui::DrawRibbon(EditorScene& scene) {
    bool goHome = false;

    const float ribbonH = 88.f;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({ vp->WorkPos.x, vp->WorkPos.y });
    ImGui::SetNextWindowSize({ vp->WorkSize.x, ribbonH });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 10.f, 8.f });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8.f, 6.f });
    ImGui::Begin("##Ribbon", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Row 1: title (left) + File/Play (right)
    if (m_fontTitle) ImGui::PushFont(m_fontTitle);
    ImGui::Text("CreateToPlay Studio");
    if (m_fontTitle) ImGui::PopFont();

    const float rightBtnsW = 130.f;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - rightBtnsW);
    ImGui::BeginDisabled();
    if (ImGui::Button("File", { 56.f, 0.f })) {}
    ImGui::SameLine();
    if (ImGui::Button("Play", { 56.f, 0.f })) {}
    ImGui::EndDisabled();

    ImGui::Separator();

    // Row 2: Home button + transform tools
    ImGui::PushStyleColor(ImGuiCol_Button,        { 0.f, 0.f, 0.f, 0.f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.22f, 0.55f, 1.f, 0.15f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.22f, 0.55f, 1.f, 0.25f });
    ImGui::PushStyleColor(ImGuiCol_Text,          { 0.40f, 0.68f, 1.f, 1.f });
    if (ImGui::Button("Home")) goHome = true;
    ImGui::PopStyleColor(4);

    ImGui::SameLine(0.f, 16.f);
    ImGui::TextUnformatted("|");
    ImGui::SameLine(0.f, 16.f);

    bool moveSel   = m_gizmoOp == ImGuizmo::TRANSLATE;
    bool rotateSel = m_gizmoOp == ImGuizmo::ROTATE;
    bool scaleSel  = m_gizmoOp == ImGuizmo::SCALE;
    if (ImGui::RadioButton("Move",   moveSel))   m_gizmoOp = ImGuizmo::TRANSLATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", rotateSel)) m_gizmoOp = ImGuizmo::ROTATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale",  scaleSel))  m_gizmoOp = ImGuizmo::SCALE;

    ImGui::End();
    ImGui::PopStyleVar(4);

    return goHome;
}

// ── Per-type coloured vector icon (13×13 px) ─────────────────────────────────
static void DrawNodeIcon(ImDrawList* dl, ImVec2 pos, float sz, EditorNodeType type) {
    const float r   = sz * 0.5f;
    const ImVec2 c  = { pos.x + r, pos.y + r };
    const float x0  = pos.x, x1 = pos.x + sz;
    const float y0  = pos.y, y1 = pos.y + sz;
    constexpr float kRnd = 2.5f;

    switch (type) {

    case EditorNodeType::DataModel: {
        // Orange diamond — "the game root"
        dl->AddQuadFilled({ c.x, y0 }, { x1, c.y }, { c.x, y1 }, { x0, c.y },
                          IM_COL32(255, 115, 45, 255));
        dl->AddQuad({ c.x, y0 }, { x1, c.y }, { c.x, y1 }, { x0, c.y },
                    IM_COL32(255, 165, 90, 140), 0.8f);
        break;
    }

    case EditorNodeType::Workspace: {
        // Teal rounded square — horizon line + sun = "world/scene"
        dl->AddRectFilled({ x0, y0 }, { x1, y1 }, IM_COL32(38, 188, 180, 255), kRnd);
        const float hy = c.y + 1.f;
        dl->AddLine({ x0 + 2.5f, hy }, { x1 - 2.5f, hy }, IM_COL32(255,255,255,170), 1.1f);
        dl->AddCircleFilled({ c.x, c.y - 2.3f }, 2.1f, IM_COL32(255,255,255,210));
        break;
    }

    case EditorNodeType::Service: {
        // Cool-grey gear: filled circle + dark centre + 4 nubs
        dl->AddCircleFilled(c, r, IM_COL32(138, 146, 163, 255));
        dl->AddCircleFilled(c, r * 0.44f, IM_COL32(38, 40, 50, 220));
        const float nubR = 1.6f, nubD = r * 0.82f;
        for (int i = 0; i < 4; i++) {
            float a = (float)i * IM_PI * 0.5f + IM_PI * 0.25f;
            dl->AddCircleFilled({ c.x + cosf(a)*nubD, c.y + sinf(a)*nubD },
                                nubR, IM_COL32(138, 146, 163, 255));
        }
        break;
    }

    case EditorNodeType::Model: {
        // Blue open-top folder / group box
        const ImU32 col = IM_COL32(80, 138, 255, 255);
        // Body rect
        dl->AddRectFilled({ x0, y0 + 3.f }, { x1, y1 }, IM_COL32(80,138,255,40), 2.f);
        dl->AddRect({ x0, y0 + 3.f }, { x1, y1 }, col, 2.f, 0, 1.5f);
        // Tab on top-left
        dl->AddRectFilled({ x0, y0 + 1.f }, { x0 + sz*0.45f, y0 + 3.5f },
                          IM_COL32(80,138,255,40), 1.f);
        dl->AddLine({ x0,          y0 + 1.f }, { x0,                   y0 + 3.5f }, col, 1.5f);
        dl->AddLine({ x0,          y0 + 1.f }, { x0 + sz*0.45f,        y0 + 1.f  }, col, 1.5f);
        dl->AddLine({ x0 + sz*0.45f, y0 + 1.f }, { x0 + sz*0.45f,      y0 + 3.5f }, col, 1.5f);
        break;
    }

    case EditorNodeType::Part: {
        // Cornflower-blue brick with subtle top highlight
        dl->AddRectFilled({ x0, y0 }, { x1, y1 }, IM_COL32(100, 148, 240, 255), kRnd);
        dl->AddLine({ x0 + kRnd, y0 + 1.f }, { x1 - kRnd, y0 + 1.f },
                    IM_COL32(200, 222, 255, 90), 1.f);
        dl->AddRect({ x0, y0 }, { x1, y1 }, IM_COL32(60,110,210,180), kRnd, 0, 0.8f);
        break;
    }

    case EditorNodeType::Script: {
        // Green pill + white ">" chevron (server-side = green)
        dl->AddRectFilled({ x0, y0 }, { x1, y1 }, IM_COL32(46, 178, 88, 255), kRnd);
        const float lx = x0 + sz*0.26f, rx = x0 + sz*0.68f;
        const float ty = c.y - sz*0.22f, by = c.y + sz*0.22f;
        dl->AddLine({ lx, ty }, { rx, c.y }, IM_COL32(215, 255, 215, 245), 1.6f);
        dl->AddLine({ rx, c.y }, { lx, by }, IM_COL32(215, 255, 215, 245), 1.6f);
        break;
    }

    case EditorNodeType::LocalScript: {
        // Amber/gold pill + white ">" chevron (client-side = gold)
        dl->AddRectFilled({ x0, y0 }, { x1, y1 }, IM_COL32(218, 162, 28, 255), kRnd);
        const float lx = x0 + sz*0.26f, rx = x0 + sz*0.68f;
        const float ty = c.y - sz*0.22f, by = c.y + sz*0.22f;
        dl->AddLine({ lx, ty }, { rx, c.y }, IM_COL32(255, 248, 195, 245), 1.6f);
        dl->AddLine({ rx, c.y }, { lx, by }, IM_COL32(255, 248, 195, 245), 1.6f);
        break;
    }

    case EditorNodeType::ModuleScript: {
        // Violet pill + two vertical "pillar" lines = module/library
        dl->AddRectFilled({ x0, y0 }, { x1, y1 }, IM_COL32(152, 68, 222, 255), kRnd);
        const float pilW = 1.8f, gap = sz * 0.19f;
        const float barY0 = y0 + 2.5f, barY1 = y1 - 2.5f;
        const ImU32 wh = IM_COL32(232, 200, 255, 240);
        dl->AddRectFilled({ c.x-gap-pilW, barY0 }, { c.x-gap, barY1 }, wh);
        dl->AddRectFilled({ c.x+gap,      barY0 }, { c.x+gap+pilW, barY1 }, wh);
        dl->AddLine({ c.x-gap, barY0+0.8f }, { c.x+gap, barY0+0.8f }, IM_COL32(232,200,255,130), 1.f);
        dl->AddLine({ c.x-gap, barY1-0.8f }, { c.x+gap, barY1-0.8f }, IM_COL32(232,200,255,130), 1.f);
        break;
    }

    default: {
        dl->AddCircleFilled(c, r, IM_COL32(118, 120, 133, 200));
        break;
    }
    }
}

void StudioGui::DrawExplorerNode(EditorScene& scene, EditorNode& node) {
    // Snapshot BEFORE the context menu can insert/delete children.
    // If NoTreePushOnOpen is set, TreeNodeEx does NOT push an indent — so we
    // must guard TreePop() with this snapshot, not node.children.empty(),
    // otherwise inserting a child mid-frame causes an ID-stack underflow assert.
    const bool wasLeaf = node.children.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (scene.GetSelected() == &node)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (wasLeaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    // Keep the top-level structural nodes expanded by default so newly
    // inserted children are immediately visible without manual expansion.
    if (node.type == EditorNodeType::DataModel ||
        node.type == EditorNodeType::Workspace ||
        node.type == EditorNodeType::Service)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    // 3-space prefix reserves room for the 13px icon drawn after the call
    char labelBuf[320];
    snprintf(labelBuf, sizeof(labelBuf), "   %s", node.name.c_str());
    bool open = ImGui::TreeNodeEx((void*)(intptr_t)&node, flags, "%s", labelBuf);

    // Overlay the icon over the prefix space, vertically centred in the row
    {
        ImVec2 iMin = ImGui::GetItemRectMin();
        ImVec2 iSz  = ImGui::GetItemRectSize();
        const float kIconSz = 13.f;
        ImVec2 iconPos = {
            iMin.x + ImGui::GetTreeNodeToLabelSpacing(),
            iMin.y + (iSz.y - kIconSz) * 0.5f
        };
        DrawNodeIcon(ImGui::GetWindowDrawList(), iconPos, kIconSz, node.type);
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        scene.SetSelected(&node);

    // Double-click opens the script editor
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (node.type == EditorNodeType::Script     ||
            node.type == EditorNodeType::LocalScript ||
            node.type == EditorNodeType::ModuleScript) {
            scene.SetSelected(&node);
            m_editingScript    = &node;
            m_scriptEditorOpen = true;
        }
    }

    if (ImGui::BeginPopupContextItem()) {
        // Select the right-clicked node immediately so that all insert
        // operations use THIS node as their parent target, not whatever
        // happened to be selected before the right-click.
        scene.SetSelected(&node);

        if (ImGui::MenuItem("Insert Part"))         scene.InsertPart();
        if (ImGui::MenuItem("Insert Model"))        scene.InsertModel();
        if (ImGui::MenuItem("Insert Script"))       scene.InsertScript();
        if (ImGui::MenuItem("Insert LocalScript"))  scene.InsertLocalScript();
        if (ImGui::MenuItem("Insert ModuleScript")) scene.InsertModuleScript();
        if (node.type == EditorNodeType::Part ||
            node.type == EditorNodeType::Model) {
            if (ImGui::MenuItem("Rename")) {
                scene.SetSelected(&node);
                m_renameOpen = true;
                strncpy(m_renameBuf, node.name.c_str(), sizeof(m_renameBuf) - 1);
                m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
            }
            if (ImGui::MenuItem("Delete")) {
                scene.SetSelected(&node);
                scene.DeleteSelected();
            }
            if (node.type == EditorNodeType::Part && node.parent) {
                scene.SetSelected(&node);
                EditorNode* ws = scene.GetWorkspace();
                if (ws) {
                    if (ImGui::MenuItem("Move to Workspace"))
                        scene.ReparentSelected(ws);
                    for (auto& c : ws->children) {
                        if (c->type == EditorNodeType::Model && c.get() != &node) {
                            std::string label = "Move to " + c->name;
                            if (ImGui::MenuItem(label.c_str()))
                                scene.ReparentSelected(c.get());
                        }
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    if (open && !wasLeaf) {
        for (auto& c : node.children)
            DrawExplorerNode(scene, *c);
        ImGui::TreePop();
    }
}

void StudioGui::DrawExplorer(EditorScene& scene) {
    ImGui::Begin("Explorer", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);
    if (scene.GetRoot())
        DrawExplorerNode(scene, *scene.GetRoot());
    ImGui::End();

    if (m_renameOpen) {
        ImGui::OpenPopup("Rename");
        m_renameOpen = false;
    }
    if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", m_renameBuf, sizeof(m_renameBuf));
        if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            if (scene.GetSelected())
                scene.GetSelected()->name = m_renameBuf;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

namespace {

glm::mat3 ExtractRotationNoScale(const glm::mat4& m) {
    glm::mat3 r(m);
    const float lx = glm::length(r[0]);
    const float ly = glm::length(r[1]);
    const float lz = glm::length(r[2]);
    if (lx > 1e-6f) r[0] /= lx;
    if (ly > 1e-6f) r[1] /= ly;
    if (lz > 1e-6f) r[2] /= lz;
    return r;
}

static void ViewportMouseRay(const StudioCamera& camera, const ImVec2& vpPos,
                             const ImVec2& vpSize, glm::vec3& ro, glm::vec3& rd) {
    ImVec2 mouse = ImGui::GetMousePos();
    float  nx    = (mouse.x - vpPos.x) / vpSize.x;
    float  ny    = (mouse.y - vpPos.y) / vpSize.y;
    nx           = nx * 2.f - 1.f;
    ny           = 1.f - ny * 2.f;

    glm::mat4 invVP = glm::inverse(camera.GetProjection() * camera.GetView());
    glm::vec4 pNear = invVP * glm::vec4(nx, ny, -1.f, 1.f);
    glm::vec4 pFar  = invVP * glm::vec4(nx, ny,  1.f, 1.f);
    pNear /= pNear.w;
    pFar  /= pFar.w;
    ro     = glm::vec3(pNear);
    rd     = glm::normalize(glm::vec3(pFar) - ro);
}

static glm::vec3 ViewportMouseOnDragPlane(const glm::vec3& planePoint,
                                          const glm::vec3& axisWorld,
                                          const StudioCamera& camera,
                                          const ImVec2& vpPos, const ImVec2& vpSize) {
    glm::vec3 ro, rd;
    ViewportMouseRay(camera, vpPos, vpSize, ro, rd);

    glm::mat4 invView = glm::inverse(camera.GetView());
    glm::vec3 eye     = glm::vec3(invView[3]);

    glm::vec3 axis = glm::normalize(axisWorld);
    glm::vec3 n    = glm::cross(axis, glm::normalize(eye - planePoint));
    if (glm::length(n) < 1e-5f)
        n = glm::cross(axis, glm::vec3(0.f, 1.f, 0.f));
    n = glm::normalize(n);

    float denom = glm::dot(rd, n);
    if (std::fabs(denom) < 1e-6f)
        return planePoint;

    float tRay = glm::dot(planePoint - ro, n) / denom;
    return ro + rd * tRay;
}

// World-space size along axis from anchored face (sign +1 = dragging positive end).
static float ScaleExtentFromMouse(const glm::vec3& anchor, const glm::vec3& axisWorld,
                                  int sign, const StudioCamera& camera,
                                  const ImVec2& vpPos, const ImVec2& vpSize) {
    glm::vec3 hit = ViewportMouseOnDragPlane(anchor, axisWorld, camera, vpPos, vpSize);
    glm::vec3 axis = glm::normalize(axisWorld);
    return glm::dot((hit - anchor) * (float)sign, axis);
}

static bool GizmoHandleClicked(ImGuizmo::OPERATION op) {
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return false;
    if (ImGuizmo::IsOver(op))
        return true;

    const ImGuizmo::MOVETYPE h = ImGuizmo::GetHoveredHandleType();
    switch (op) {
    case ImGuizmo::TRANSLATE:
        return h >= ImGuizmo::MT_MOVE_X && h <= ImGuizmo::MT_MOVE_SCREEN;
    case ImGuizmo::ROTATE:
        return h >= ImGuizmo::MT_ROTATE_X && h <= ImGuizmo::MT_ROTATE_SCREEN;
    case ImGuizmo::SCALE:
        return h >= ImGuizmo::MT_SCALE_X && h <= ImGuizmo::MT_SCALE_Z;
    default:
        return false;
    }
}

static bool ConsumeGizmoApplySkip(bool& skipFlag) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        return false;
    if (skipFlag) {
        skipFlag = false;
        return false;
    }
    return true;
}

constexpr float kPropCellGap = 4.f;

void BeginPropertiesTable() {
    const ImGuiTableFlags flags =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.f, 5.f));
    ImGui::BeginTable("##properties", 2, flags);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 108.f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
}

void EndPropertiesTable() {
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

bool PropCollapsingSection(const char* title) {
    const ImVec4 panel = { 0.15f, 0.15f, 0.16f, 1.f };
    ImGui::PushStyleColor(ImGuiCol_Header, panel);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, panel);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, panel);
    ImGui::PushStyleColor(ImGuiCol_Text, { 0.65f, 0.68f, 0.72f, 1.f });
    const bool open =
        ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(4);
    return open;
}

void PropLabel(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(label);
}

void PropRowEnd() {
    ImGui::PopID();
}

void PropFullRowSeparator() {
    ImGuiTable* table = ImGui::GetCurrentTable();
    if (!table) return;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const float y    = ImGui::GetCursorScreenPos().y + 1.f;
    const float rowH = 7.f;
    ImGui::Dummy(ImVec2(1.f, rowH));
    ImGui::TableSetColumnIndex(1);
    ImGui::Dummy(ImVec2(1.f, rowH));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(table->OuterRect.Min.x, y), ImVec2(table->OuterRect.Max.x, y),
                ImGui::GetColorU32(ImGuiCol_Separator), 1.f);
}

void PushValueWrapStyle() {
    const ImVec4 wrapBg = { 0.11f, 0.11f, 0.12f, 1.f };
    ImGui::PushStyleColor(ImGuiCol_ChildBg, wrapBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, wrapBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, wrapBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, wrapBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 3.f));
}

void PopValueWrapStyle() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

float PropValueCellWidth(int count) {
    float avail = ImGui::GetContentRegionAvail().x;
    if (count < 1) count = 1;
    return (avail - kPropCellGap * (float)(count - 1)) / (float)count;
}

void PropFloatCell(const char* cellId, float width, float* v, float speed,
                   float vMin = 0.f, float vMax = 0.f, const char* fmt = "%.3f") {
    ImGui::PushID(cellId);
    PushValueWrapStyle();
    ImGui::BeginChild("##cell", ImVec2(width, 0.f),
                      ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat("##v", v, speed, vMin, vMax, fmt);
    ImGui::EndChild();
    PopValueWrapStyle();
    ImGui::PopID();
}

bool PropFloat3Cells(float* xyz, float speed, float vMin = 0.f, float vMax = 0.f,
                     const char* fmt = "%.3f") {
    const char* axes[] = { "X", "Y", "Z" };
    float       cellW  = PropValueCellWidth(3);
    bool        changed = false;
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0.f, kPropCellGap);
        float before = xyz[i];
        PropFloatCell(axes[i], cellW, &xyz[i], speed, vMin, vMax, fmt);
        if (xyz[i] != before) changed = true;
    }
    return changed;
}

void PropReadOnlyCell(float width, const char* text) {
    PushValueWrapStyle();
    ImGui::BeginChild("##cell", ImVec2(width, 0.f),
                      ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::PushStyleColor(ImGuiCol_Text, { 0.55f, 0.58f, 0.62f, 1.f });
    ImGui::SetNextItemWidth(-FLT_MIN);
    char buf[64];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    ImGui::InputText("##readonly", buf, sizeof(buf),
                     ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    PopValueWrapStyle();
}

bool PropTextCell(float width, char* buf, int bufSize) {
    PushValueWrapStyle();
    ImGui::BeginChild("##cell", ImVec2(width, 0.f),
                      ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ImGui::InputText("##v", buf, (size_t)bufSize);
    ImGui::EndChild();
    PopValueWrapStyle();
    return changed;
}

void PropCheckboxCell(bool* v) {
    constexpr float kSize     = 22.f;
    constexpr float kRounding = 5.f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - kSize);

    const ImVec4 wrapBg = { 0.11f, 0.11f, 0.12f, 1.f };
    if (ImGui::InvisibleButton("##anchored_btn", ImVec2(kSize, kSize)))
        *v = !*v;

    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    ImDrawList*  dl   = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rmin, rmax, ImGui::ColorConvertFloat4ToU32(wrapBg), kRounding);
    dl->AddRect(rmin, rmax, ImGui::GetColorU32(ImGuiCol_Border), kRounding, 0, 1.f);

    if (*v) {
        constexpr float kInset = 3.f;
        const ImU32   fillCol = ImGui::GetColorU32(ImGuiCol_CheckMark);
        const ImVec2  iMin(rmin.x + kInset, rmin.y + kInset);
        const ImVec2  iMax(rmax.x - kInset, rmax.y - kInset);
        const float   iRound = std::max(3.f, kRounding - 1.f);
        dl->AddRectFilled(iMin, iMax, fillCol, iRound);
    }
}

}  // namespace

void StudioGui::DrawProperties(EditorScene& scene) {
    ImGui::Begin("Properties", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);
    EditorNode* sel = scene.GetSelected();
    if (!sel) {
        ImGui::TextDisabled("Select an object in the Explorer or Viewport.");
        ImGui::End();
        return;
    }

    if (PropCollapsingSection("General")) {
        BeginPropertiesTable();
        PropLabel("Name");
        char nameBuf[128];
        strncpy(nameBuf, sel->name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (PropTextCell(PropValueCellWidth(1), nameBuf, sizeof(nameBuf)))
            sel->name = nameBuf;
        PropRowEnd();
        PropFullRowSeparator();

        if (sel->type == EditorNodeType::Part) {
            PropLabel("UniqueID");
            const char* idText = sel->uniqueId.empty() ? "—" : sel->uniqueId.c_str();
            PropReadOnlyCell(PropValueCellWidth(1), idText);
            PropRowEnd();
        }
        EndPropertiesTable();
    }

    if (sel->type == EditorNodeType::Workspace ||
        sel->type == EditorNodeType::Service   ||
        sel->type == EditorNodeType::DataModel) {
        ImGui::Spacing();
        ImGui::TextDisabled("No editable properties.");
        ImGui::End();
        return;
    }

    // Script nodes — show open button + enabled toggle
    if (sel->type == EditorNodeType::Script     ||
        sel->type == EditorNodeType::LocalScript ||
        sel->type == EditorNodeType::ModuleScript) {
        const char* typeName =
            sel->type == EditorNodeType::Script       ? "ServerScript" :
            sel->type == EditorNodeType::LocalScript  ? "LocalScript"  : "ModuleScript";
        ImGui::TextDisabled("%s", typeName);
        ImGui::Spacing();
        const ImVec4 kBlue  = { 0.22f, 0.55f, 1.00f, 1.f };
        const ImVec4 kBlueH = { 0.30f, 0.62f, 1.00f, 1.f };
        const ImVec4 kBlueA = { 0.18f, 0.48f, 0.90f, 1.f };
        ImGui::PushStyleColor(ImGuiCol_Button,        kBlue);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBlueH);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kBlueA);
        if (ImGui::Button("Open Script", { -FLT_MIN, 0.f })) {
            m_editingScript    = sel;
            m_scriptEditorOpen = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::Spacing();
        if (PropCollapsingSection("Script")) {
            BeginPropertiesTable();
            PropLabel("Name");
            char nb[128]; strncpy(nb, sel->name.c_str(), 127); nb[127] = '\0';
            if (PropTextCell(PropValueCellWidth(1), nb, sizeof(nb)))
                sel->name = nb;
            PropRowEnd();
            PropFullRowSeparator();
            PropLabel("Enabled");
            PropCheckboxCell(&sel->scriptEnabled);
            PropRowEnd();
            EndPropertiesTable();
        }
        ImGui::End();
        return;
    }

    if (PropCollapsingSection("Transform")) {
        BeginPropertiesTable();
        if (sel->type == EditorNodeType::Part) {
            PropLabel("Position");
            glm::vec3 prev = sel->position;
            if (PropFloat3Cells(&sel->position.x, 0.1f) && sel->position != prev)
                scene.ApplyGizmoTranslation(*sel, scene.PartWorldCenter(*sel));
            PropRowEnd();
            PropFullRowSeparator();
        }

        PropLabel("Rotation");
        PropFloat3Cells(&sel->eulerDeg.x, 0.5f);
        PropRowEnd();
        PropFullRowSeparator();

        if (sel->type == EditorNodeType::Part) {
            PropLabel("Size");
            glm::vec3 prevSize = sel->size;
            if (PropFloat3Cells(&sel->size.x, 0.1f, 0.1f, 512.f) && sel->size != prevSize)
                scene.SetPartWorldCenter(*sel, scene.PartWorldCenter(*sel));
            PropRowEnd();
        }
        EndPropertiesTable();
    }

    if (sel->type == EditorNodeType::Part && PropCollapsingSection("Appearance")) {
        BeginPropertiesTable();
        PropLabel("Color");
        PropFloat3Cells(&sel->color.x, 0.01f, 0.f, 1.f);
        PropRowEnd();
        PropFullRowSeparator();

        PropLabel("Reflectance");
        PropFloatCell("R", PropValueCellWidth(1), &sel->reflectance, 0.01f, 0.f, 1.f, "%.2f");
        PropRowEnd();
        PropFullRowSeparator();

        PropLabel("Anchored");
        PropCheckboxCell(&sel->anchored);
        PropRowEnd();
        EndPropertiesTable();
    }

    ImGui::End();
}

static bool RayHitsPart(const glm::vec3& ro, const glm::vec3& rd, EditorNode& part, float& tOut) {
    glm::mat4 wm = part.WorldMatrix();
    glm::vec3 half = part.size * 0.5f;
    glm::vec3 corners[8];
    int i = 0;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2)
                corners[i++] = glm::vec3(wm * glm::vec4(half.x * (float)sx, half.y * (float)sy,
                                                             half.z * (float)sz, 1.f));
    glm::vec3 bmin = corners[0];
    glm::vec3 bmax = corners[0];
    for (int c = 1; c < 8; ++c) {
        bmin = glm::min(bmin, corners[c]);
        bmax = glm::max(bmax, corners[c]);
    }
    return RayAABB(ro, rd, bmin, bmax, tOut);
}

void StudioGui::HandleViewportPick(EditorScene& scene, StudioCamera& camera,
                                   const ImVec2& vpPos, const ImVec2& vpSize) {
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (m_gizmoWasUsing || m_scaleGizmoActive) return;
    if (ImGuizmo::IsUsing() || ImGuizmo::IsUsingAny()) return;
    // Only block pick when we actually drew a gizmo this frame (avoids stale IsOver).
    if (m_gizmoDrawnThisFrame && ImGuizmo::IsOver()) return;

    ImVec2 mouse = ImGui::GetMousePos();
    float nx = (mouse.x - vpPos.x) / vpSize.x;
    float ny = (mouse.y - vpPos.y) / vpSize.y;
    if (nx < 0.f || nx > 1.f || ny < 0.f || ny > 1.f) return;

    nx = nx * 2.f - 1.f;
    ny = 1.f - ny * 2.f;

    glm::mat4 invVP = glm::inverse(camera.GetProjection() * camera.GetView());
    glm::vec4 pNear = invVP * glm::vec4(nx, ny, -1.f, 1.f);
    glm::vec4 pFar  = invVP * glm::vec4(nx, ny,  1.f, 1.f);
    pNear /= pNear.w;
    pFar  /= pFar.w;

    glm::vec3 ro = glm::vec3(pNear);
    glm::vec3 rd = glm::normalize(glm::vec3(pFar) - ro);

    // Closest hit wins (parts on top of baseplate beat the floor).
    EditorNode* best  = nullptr;
    float       bestT = 1e9f;

    scene.ForEachPart([&](EditorNode& part) {
        float t = 0.f;
        if (RayHitsPart(ro, rd, part, t) && t < bestT) {
            bestT = t;
            best  = &part;
        }
    });

    if (!best) return;

    EditorNode* current = scene.GetSelected();
    if (current && current != best && best->name == "Baseplate" &&
        current->type == EditorNodeType::Part && current->name != "Baseplate")
        return;

    scene.SetSelected(best);
}

void StudioGui::DrawViewport(EditorScene& scene, StudioCamera& camera,
                             StudioRenderer& renderer) {
    ImGui::Begin("Viewport", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);

    ImVec2 size = ImGui::GetContentRegionAvail();
    int w = (int)size.x;
    int h = (int)size.y;
    if (w < 8) w = 8;
    if (h < 8) h = 8;

    if (w != renderer.Width() || h != renderer.Height()) {
        renderer.Resize(w, h);
        camera.SetViewportSize(w, h);
    }

    // Dummy reserves layout + hover; image drawn after scene render + gizmo input.
    ImGui::Dummy(size);
    m_viewportHovered = ImGui::IsItemHovered();
    m_viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    ImVec2 vpPos  = ImGui::GetItemRectMin();
    ImVec2 vpMax  = ImGui::GetItemRectMax();
    ImVec2 vpSize = ImVec2(vpMax.x - vpPos.x, vpMax.y - vpPos.y);

    m_gizmoDrawnThisFrame = false;
    bool sceneChanged     = false;

    // ── Gizmo tool keybinds (1/2/3) — only when viewport focused, not typing ──
    if (m_viewportFocused && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) m_gizmoOp = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) m_gizmoOp = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) m_gizmoOp = ImGuizmo::SCALE;
    }

    renderer.BeginFrame(camera);
    renderer.DrawScene(scene, scene.GetSelected());
    renderer.DrawGrid();
    renderer.EndFrame();

    ImGui::GetWindowDrawList()->AddImage(
        (ImTextureID)(intptr_t)renderer.GetColorTexture(),
        vpPos, vpMax, ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::Enable(true);
    ImGuizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y);
    ImGuizmo::SetAlternativeWindow(ImGui::GetCurrentWindow());

    EditorNode* sel = scene.GetSelected();
    bool rmbHeld = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool allowGizmo = m_viewportHovered || ImGuizmo::IsUsing() ||
                      ImGuizmo::IsUsingAny() || m_scaleGizmoActive ||
                      m_axisDragActive;
    if (sel && sel->type == EditorNodeType::Part && allowGizmo && !rmbHeld) {
        const glm::mat4 worldBefore = sel->WorldTRMatrix();
        float matrix[16];
        memcpy(matrix, glm::value_ptr(worldBefore), sizeof(matrix));

        ConfigureImGuizmoForOp(m_gizmoOp);
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::PushID(sel);
        m_gizmoDrawnThisFrame = true;

        ImGuizmo::Manipulate(
            glm::value_ptr(camera.GetView()),
            glm::value_ptr(camera.GetProjection()),
            m_gizmoOp, m_gizmoMode, matrix);

        // ── SCALE ─────────────────────────────────────────────────────────────
        // We deliberately ignore ImGuizmo's output matrix for scale — reading
        // scale from the matrix while SetPartWorldCenter mutates the input
        // matrix every frame causes teleporting.  Instead we track raw mouse
        // pixel movement from the drag-start position and project it onto the
        // screen-space axis direction to compute a world-unit delta.  This also
        // naturally fixes inversion: dragging outward from either face always
        // produces a positive grow delta regardless of camera orientation.
        if (m_gizmoOp == ImGuizmo::SCALE) {
            bool gizmoUsing = ImGuizmo::IsUsing() || ImGuizmo::IsUsingAny();
            if (gizmoUsing) {
                if (!m_scaleGizmoActive) {
                    // ── First frame: snapshot drag-start state ────────────────
                    m_scaleGizmoActive      = true;
                    m_gizmoScaleStartSize   = sel->size;
                    m_gizmoScaleStartCenter = scene.PartWorldCenter(*sel);

                    // Which axis?  GetHoveredHandleType returns MT_NONE the
                    // same frame IsUsing() goes true, so fall back to
                    // GetActiveHandleType.
                    int axis = ImGuizmo::GetHoveredHandleType()
                               - ImGuizmo::MT_SCALE_X;
                    if (axis < 0 || axis > 2)
                        axis = ImGuizmo::GetActiveHandleType()
                               - ImGuizmo::MT_SCALE_X;
                    if (axis < 0 || axis > 2) axis = 0;
                    m_gizmoScaleAxis = axis;

                    // World-space direction of the grabbed local axis.
                    glm::mat4 tr = sel->WorldTRMatrix();
                    m_gizmoScaleAxisWorld =
                        glm::normalize(glm::vec3(tr[m_gizmoScaleAxis]));

                    // Helper: 3-D world point → screen pixel position.
                    auto toScreen = [&](const glm::vec3& wp) -> glm::vec2 {
                        glm::vec4 c = camera.GetProjection()
                                      * camera.GetView()
                                      * glm::vec4(wp, 1.f);
                        c /= c.w;
                        return {
                            (c.x * 0.5f + 0.5f) * vpSize.x + vpPos.x,
                            (1.f - (c.y * 0.5f + 0.5f)) * vpSize.y + vpPos.y
                        };
                    };

                    // Normalised screen-space direction of the positive axis.
                    glm::vec2 sc    = toScreen(m_gizmoScaleStartCenter);
                    glm::vec2 sa    = toScreen(m_gizmoScaleStartCenter
                                               + m_gizmoScaleAxisWorld);
                    glm::vec2 axDir = sa - sc;
                    float     axLen = glm::length(axDir);
                    m_gizmoScaleAxDirPx = (axLen > 1e-6f)
                        ? axDir / axLen
                        : glm::vec2{ 1.f, 0.f };

                    // Save mouse position at drag-start.
                    ImVec2 mp = ImGui::GetMousePos();
                    m_gizmoScaleMouseStart = { mp.x, mp.y };

                    // Sign: which face (+1 or -1)?
                    // The mouse is at the dragged handle, so the vector from
                    // center→mouse tells us which half of the axis we're on.
                    glm::vec2 mRel = { mp.x - sc.x, mp.y - sc.y };
                    float dot = mRel.x * m_gizmoScaleAxDirPx.x
                              + mRel.y * m_gizmoScaleAxDirPx.y;
                    m_gizmoScaleSign = (dot >= 0.f) ? 1 : -1;
                }

                // ── Every drag frame: derive size from accumulated mouse delta ─
                ImVec2 mouseNow = ImGui::GetMousePos();
                glm::vec2 totalDelta = {
                    mouseNow.x - m_gizmoScaleMouseStart.x,
                    mouseNow.y - m_gizmoScaleMouseStart.y
                };

                // Project delta onto the axis; multiply by sign so that
                // "outward from either face" always means a positive value.
                float axisDeltaPx =
                    (totalDelta.x * m_gizmoScaleAxDirPx.x
                   + totalDelta.y * m_gizmoScaleAxDirPx.y)
                    * (float)m_gizmoScaleSign;

                // Pixels → world units via perspective:
                //   worldPerPx = 2 * dist / (proj[1][1] * vpHeight)
                float distFromCam = glm::length(
                    camera.GetPosition() - m_gizmoScaleStartCenter);
                float focalLen    = camera.GetProjection()[1][1]
                                    * vpSize.y * 0.5f;
                float worldDelta  = (focalLen > 1e-6f)
                    ? axisDeltaPx * distFromCam / focalLen
                    : 0.f;

                float newAxisSize = std::max(
                    m_gizmoScaleStartSize[m_gizmoScaleAxis] + worldDelta,
                    0.05f);

                // Only the dragged axis changes; reset others to start values.
                sel->size = m_gizmoScaleStartSize;
                sel->size[m_gizmoScaleAxis] = newAxisSize;

                // Shift held → symmetric scale: both faces move, center stays.
                // No Shift    → one-sided: anchor face stays, dragged face moves.
                const bool symmetric = ImGui::GetIO().KeyShift;
                glm::vec3 newCenter = m_gizmoScaleStartCenter;
                if (!symmetric) {
                    float halfDelta =
                        (newAxisSize - m_gizmoScaleStartSize[m_gizmoScaleAxis])
                        * 0.5f;
                    newCenter += m_gizmoScaleAxisWorld
                                 * halfDelta * (float)m_gizmoScaleSign;
                }
                scene.SetPartWorldCenter(*sel, newCenter);

                sceneChanged = true;
            } else {
                m_scaleGizmoActive = false;
            }
        }

        // ── TRANSLATE / ROTATE ────────────────────────────────────────────────
        if (m_gizmoOp != ImGuizmo::SCALE && GizmoHandleClicked(m_gizmoOp)) {
            m_axisDragActive       = true;
            m_gizmoSkipApply       = true;
            m_gizmoDragOrigin      = glm::vec3(worldBefore[3]);
            m_gizmoDragStartCenter = scene.PartWorldCenter(*sel);
        } else if (m_gizmoOp != ImGuizmo::SCALE &&
                   (ImGuizmo::IsUsing() || ImGuizmo::IsUsingAny()) && !m_axisDragActive) {
            m_axisDragActive       = true;
            m_gizmoSkipApply       = true;
            m_gizmoDragOrigin      = glm::vec3(worldBefore[3]);
            m_gizmoDragStartCenter = scene.PartWorldCenter(*sel);
        }

        if (m_axisDragActive && ConsumeGizmoApplySkip(m_gizmoSkipApply)) {
            glm::mat4 newWorld;
            memcpy(glm::value_ptr(newWorld), matrix, sizeof(matrix));

            if (m_gizmoOp == ImGuizmo::TRANSLATE) {
                const glm::vec3 delta = glm::vec3(newWorld[3]) - m_gizmoDragOrigin;
                scene.ApplyGizmoTranslation(*sel, m_gizmoDragStartCenter + delta);
            } else if (m_gizmoOp == ImGuizmo::ROTATE) {
                glm::mat4 parentWorld(1.f);
                if (sel->parent && sel->parent->type != EditorNodeType::Workspace)
                    parentWorld = sel->parent->WorldMatrix();

                const glm::mat4 local = glm::inverse(parentWorld) * newWorld;
                sel->SetRotationFromMatrix(ExtractRotationNoScale(local));
                scene.SetPartWorldCenter(*sel, m_gizmoDragStartCenter);
            }
            sceneChanged = true;
        }

        ImGuizmo::PopID();
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_scaleGizmoActive = false;
        m_axisDragActive   = false;
        m_gizmoSkipApply   = false;
    }

    m_gizmoWasUsing = ImGuizmo::IsUsing() || ImGuizmo::IsUsingAny() ||
                      m_scaleGizmoActive || m_axisDragActive;

    if (sceneChanged) {
        renderer.BeginFrame(camera);
        renderer.DrawScene(scene, scene.GetSelected());
        renderer.DrawGrid();
        renderer.EndFrame();

        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)(intptr_t)renderer.GetColorTexture(),
            vpPos, vpMax, ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));
    }

    HandleViewportPick(scene, camera, vpPos, vpSize);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Script editor (replaces viewport when a script is open)
// ─────────────────────────────────────────────────────────────────────────────

// std::string-backed InputTextMultiline (resizes automatically).
static bool CodeInputText(const char* id, std::string* str,
                           const ImVec2& sz, ImFont* font) {
    struct Cb {
        static int Fn(ImGuiInputTextCallbackData* d) {
            if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* s = (std::string*)d->UserData;
                s->resize(d->BufTextLen);
                d->Buf = s->data();
            }
            return 0;
        }
    };
    if (font) ImGui::PushFont(font);
    // Ensure there is always room for a null terminator past size.
    if (str->capacity() == str->size()) str->reserve(str->size() + 128);
    const bool changed = ImGui::InputTextMultiline(
        id, str->data(), str->capacity() + 1, sz,
        ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput,
        Cb::Fn, str);
    if (font) ImGui::PopFont();
    return changed;
}

void StudioGui::DrawScriptEditor(EditorScene& scene) {
    // If the editing node was deleted, close the editor.
    if (!m_editingScript) { m_scriptEditorOpen = false; }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   { 0.f, 0.f });
    ImGui::Begin("##ScriptEditor", nullptr,
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(3);

    const ImVec2 winSize = ImGui::GetContentRegionAvail();
    const float  headerH = 40.f;

    // ── Header bar ────────────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{ 0.12f, 0.12f, 0.13f, 1.f });
    ImGui::BeginChild("##SEHeader", { winSize.x, headerH }, ImGuiChildFlags_None);
    ImGui::PopStyleColor();

    // Back button
    ImGui::SetCursorPos({ 10.f, (headerH - ImGui::GetFrameHeight()) * 0.5f });
    ImGui::PushStyleColor(ImGuiCol_Button,        { 0.f, 0.f, 0.f, 0.f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.22f, 0.55f, 1.f, 0.15f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.22f, 0.55f, 1.f, 0.25f });
    if (ImGui::Button("← Viewport")) {
        m_scriptEditorOpen = false;
        m_editingScript    = nullptr;
    }
    ImGui::PopStyleColor(3);

    // Script type badge + name
    if (m_editingScript) {
        const char* badge =
            m_editingScript->type == EditorNodeType::Script       ? "ServerScript" :
            m_editingScript->type == EditorNodeType::LocalScript  ? "LocalScript"  :
                                                                     "ModuleScript";
        ImGui::SameLine(0.f, 16.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.40f, 0.68f, 1.f, 1.f });
        ImGui::TextUnformatted(badge);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.f, 8.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.80f, 0.82f, 0.86f, 1.f });
        ImGui::TextUnformatted(m_editingScript->name.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild(); // header

    // ── Code area ─────────────────────────────────────────────────────────────
    const float codeH = winSize.y - headerH - 1.f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg,    ImVec4{ 0.09f, 0.09f, 0.10f, 1.f });
    ImGui::PushStyleColor(ImGuiCol_FrameBg,    ImVec4{ 0.09f, 0.09f, 0.10f, 1.f });
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,ImVec4{ 0.09f, 0.09f, 0.10f, 1.f });
    ImGui::BeginChild("##SECode", { winSize.x, codeH }, ImGuiChildFlags_None);

    // Thin left gutter for visual separation
    const float gutterW = 48.f;
    const float lineH   = (m_fontMono ? m_fontMono : ImGui::GetFont())->FontSize + 2.f;
    ImDrawList* dl      = ImGui::GetWindowDrawList();

    // Gutter background
    ImVec2 gutterMin = ImGui::GetCursorScreenPos();
    ImVec2 gutterMax = { gutterMin.x + gutterW, gutterMin.y + codeH };
    dl->AddRectFilled(gutterMin, gutterMax, IM_COL32(13, 13, 15, 255));
    dl->AddLine({ gutterMax.x, gutterMin.y }, gutterMax,
                IM_COL32(34, 36, 42, 255), 1.f);

    // Line numbers (drawn over the gutter)
    if (m_editingScript) {
        int lineCount = 1;
        for (char c : m_editingScript->source) if (c == '\n') lineCount++;

        if (m_fontMono) ImGui::PushFont(m_fontMono);
        for (int i = 1; i <= lineCount; i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", i);
            ImVec2 numSz = ImGui::CalcTextSize(buf);
            ImVec2 numPos = {
                gutterMin.x + gutterW - numSz.x - 8.f,
                gutterMin.y + (i - 1) * lineH + 4.f
            };
            dl->AddText(numPos, IM_COL32(80, 85, 100, 255), buf);
        }
        if (m_fontMono) ImGui::PopFont();
    }

    // Code InputText offset to the right of the gutter
    ImGui::SetCursorPosX(gutterW + 6.f);
    ImGui::SetCursorPosY(4.f);
    if (m_editingScript) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.85f, 0.87f, 0.90f, 1.f });
        CodeInputText("##code", &m_editingScript->source,
                      { winSize.x - gutterW - 6.f, codeH - 8.f },
                      m_fontMono);
        ImGui::PopStyleColor();
    }

    ImGui::EndChild(); // code
    ImGui::PopStyleColor(3);

    ImGui::End();
}

void StudioGui::DrawDockedPanels(EditorScene& scene, StudioCamera& camera,
                                 StudioRenderer& renderer) {
    const float ribbonH = 88.f;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float panelX = vp->WorkPos.x;
    const float panelY = vp->WorkPos.y + ribbonH;
    const float panelW   = vp->WorkSize.x;
    const float panelH   = vp->WorkSize.y - ribbonH;
    const float panelGap = 10.f;
    const float sideW    = panelW * 0.28f;
    const float mainW    = panelW - sideW - panelGap;

    ImGui::SetNextWindowPos({ panelX, panelY });
    ImGui::SetNextWindowSize({ mainW, panelH });
    if (m_scriptEditorOpen && m_editingScript)
        DrawScriptEditor(scene);
    else
        DrawViewport(scene, camera, renderer);

    const float sideX     = panelX + mainW + panelGap;
    const float sideStack = panelH - panelGap;
    const float sideHalf  = sideStack * 0.5f;

    ImGui::SetNextWindowPos({ sideX, panelY });
    ImGui::SetNextWindowSize({ sideW, sideHalf });
    DrawExplorer(scene);

    ImGui::SetNextWindowPos({ sideX, panelY + sideHalf + panelGap });
    ImGui::SetNextWindowSize({ sideW, sideHalf });
    DrawProperties(scene);
}

void StudioGui::EndFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ─────────────────────────────────────────────────────────────────────────────
// Login screen
// ─────────────────────────────────────────────────────────────────────────────
bool StudioGui::DrawLoginScreen(std::string& outUsername) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   { 0.f, 0.f });
    ImGui::Begin("##LoginBg", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(3);

    // ── Angled grid background ─────────────────────────────────────────────────
    {
        ImDrawList* bg    = ImGui::GetWindowDrawList();
        const float angle = 10.f * IM_PI / 180.f;  // tilt in radians
        const float cosA  = cosf(angle);
        const float sinA  = sinf(angle);
        const float sqSz  = 96.f;   // visible square size
        const float gap   = 14.f;   // gap between squares
        const float step  = sqSz + gap;
        const float rnd   = 14.f;   // corner radius
        const float hw    = sqSz * 0.5f - rnd;  // arc-centre half-extent

        // Pivot = screen centre
        const float pcx = vp->WorkPos.x + vp->WorkSize.x * 0.5f;
        const float pcy = vp->WorkPos.y + vp->WorkSize.y * 0.5f;

        // Extend far enough to cover the screen regardless of rotation
        const float diag = sqrtf(vp->WorkSize.x * vp->WorkSize.x +
                                  vp->WorkSize.y * vp->WorkSize.y) * 0.5f + step * 2.f;
        const int ext = (int)(diag / step) + 1;

        // Very subtle colour: slightly lighter than the dark window background
        const ImU32 sqCol = IM_COL32(28, 29, 34, 255);

        // For each corner: local arc-centre direction (±1,±1) × hw,
        // and the arc's start/end angles in local (un-rotated) space.
        struct ArcDef { float dx, dy, a0, a1; };
        static constexpr ArcDef kArcs[4] = {
            { -1.f, -1.f,  IM_PI,        IM_PI * 1.5f },   // top-left
            {  1.f, -1.f,  IM_PI * 1.5f, IM_PI * 2.f  },   // top-right
            {  1.f,  1.f,  0.f,          IM_PI * 0.5f },   // bottom-right
            { -1.f,  1.f,  IM_PI * 0.5f, IM_PI        },   // bottom-left
        };

        const float bndR = sqSz * 0.7072f; // bounding-circle radius (half-diagonal)
        const float scrX0 = vp->WorkPos.x, scrX1 = scrX0 + vp->WorkSize.x;
        const float scrY0 = vp->WorkPos.y, scrY1 = scrY0 + vp->WorkSize.y;

        for (int row = -ext; row <= ext; row++) {
            for (int col = -ext; col <= ext; col++) {
                // Square centre in pre-rotation grid space
                const float lx = col * step;
                const float ly = row * step;

                // Rotate to world space
                const float wx = lx * cosA - ly * sinA + pcx;
                const float wy = lx * sinA + ly * cosA + pcy;

                // Broad-phase cull
                if (wx + bndR < scrX0 || wx - bndR > scrX1 ||
                    wy + bndR < scrY0 || wy - bndR > scrY1)
                    continue;

                // Build a rounded rotated square via four arcs
                bg->PathClear();
                for (const auto& a : kArcs) {
                    // Arc centre in local space then rotated to world space
                    const float alx = a.dx * hw;
                    const float aly = a.dy * hw;
                    const float awx = alx * cosA - aly * sinA + wx;
                    const float awy = alx * sinA + aly * cosA + wy;
                    // Angles also rotate by the grid angle
                    bg->PathArcTo({ awx, awy }, rnd,
                                  a.a0 + angle, a.a1 + angle, 3);
                }
                bg->PathFillConvex(sqCol);
            }
        }
    }

    const float cardW  = 360.f;
    const float cardH  = 220.f;
    const float logoH  = 70.f;   // space above the card for the logo
    const float totalH = logoH + cardH;
    const float cx     = std::floor((vp->WorkSize.x - cardW) * 0.5f);
    const float cy     = std::floor((vp->WorkSize.y - totalH) * 0.5f);

    // ── Logo ──────────────────────────────────────────────────────────────────
    if (m_fontTitle) ImGui::PushFont(m_fontTitle);
    const ImVec2 logoSz = ImGui::CalcTextSize("CreateToPlay");
    ImGui::SetCursorPos({ cx + (cardW - logoSz.x) * 0.5f, cy });
    ImGui::TextUnformatted("CreateToPlay");
    if (m_fontTitle) ImGui::PopFont();

    const ImVec2 subSz = ImGui::CalcTextSize("Studio");
    ImGui::SetCursorPos({ cx + (cardW - subSz.x) * 0.5f, cy + logoSz.y + 4.f });
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.50f, 0.52f, 0.58f, 1.f });
    ImGui::TextUnformatted("Studio");
    ImGui::PopStyleColor();

    // ── Card ──────────────────────────────────────────────────────────────────
    ImGui::SetCursorPos({ cx, cy + logoH });
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,  1.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{ 0.12f, 0.12f, 0.13f, 1.f });
    ImGui::BeginChild("##LoginCard", { cardW, cardH }, ImGuiChildFlags_Border);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    const float pad    = 32.f;
    const float innerW = cardW - pad * 2.f;

    ImGui::Dummy({ 0.f, 26.f });

    // Username
    ImGui::SetCursorPosX(pad);
    ImGui::SetNextItemWidth(innerW);
    ImGui::InputTextWithHint("##lu", "Username", m_loginUsername, sizeof(m_loginUsername));

    ImGui::Dummy({ 0.f, 10.f });

    // Password
    ImGui::SetCursorPosX(pad);
    ImGui::SetNextItemWidth(innerW);
    ImGui::InputTextWithHint("##lp", "Password", m_loginPassword, sizeof(m_loginPassword),
                              ImGuiInputTextFlags_Password);

    ImGui::Dummy({ 0.f, 22.f });

    // Log In (blue)
    const ImVec4 kBlue  = { 0.22f, 0.55f, 1.00f, 1.f };
    const ImVec4 kBlueH = { 0.30f, 0.62f, 1.00f, 1.f };
    const ImVec4 kBlueA = { 0.18f, 0.48f, 0.90f, 1.f };
    ImGui::SetCursorPosX(pad);
    ImGui::PushStyleColor(ImGuiCol_Button,        kBlue);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBlueH);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kBlueA);
    const bool logIn = ImGui::Button("Log In", { innerW, 36.f });
    ImGui::PopStyleColor(3);

    ImGui::EndChild();
    ImGui::End();

    if (logIn) {
        outUsername = m_loginUsername[0] != '\0'
                      ? std::string(m_loginUsername) : "User";
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Home / game-library screen
// ─────────────────────────────────────────────────────────────────────────────
int StudioGui::DrawHomeScreen(const std::string& username,
                               const std::vector<GameEntry>& games) {
    int result = 0;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    { 0.f, 0.f });
    ImGui::Begin("##Home", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(3);

    const float sideW  = 224.f;
    const float winH   = vp->WorkSize.y;
    const ImVec4 kBlue  = { 0.22f, 0.55f, 1.00f, 1.f };
    const ImVec4 kBlueH = { 0.30f, 0.62f, 1.00f, 1.f };
    const ImVec4 kBlueA = { 0.18f, 0.48f, 0.90f, 1.f };

    // ── Sidebar ───────────────────────────────────────────────────────────────
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{ 0.12f, 0.12f, 0.13f, 1.f });
    ImGui::BeginChild("##HSide", { sideW, winH }, ImGuiChildFlags_Border);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImDrawList* sdl = ImGui::GetWindowDrawList();

    // Avatar + username
    ImGui::Dummy({ 0.f, 22.f });
    ImGui::SetCursorPosX(18.f);
    {
        const float avR = 18.f;
        ImVec2 avScr = ImGui::GetCursorScreenPos();
        ImVec2 avCtr = { avScr.x + avR, avScr.y + avR };
        sdl->AddCircleFilled(avCtr, avR, IM_COL32(0x35, 0x6A, 0xFF, 210));
        const char init[2] = {
            (char)std::toupper((unsigned char)(username.empty() ? 'U' : username[0])),
            '\0'
        };
        ImVec2 isz = ImGui::CalcTextSize(init);
        sdl->AddText({ avCtr.x - isz.x * 0.5f, avCtr.y - isz.y * 0.5f },
                     IM_COL32(255, 255, 255, 255), init);
        ImGui::Dummy({ avR * 2.f, avR * 2.f });
    }

    ImGui::SetCursorPosX(18.f);
    ImGui::TextUnformatted(username.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.45f, 0.47f, 0.52f, 1.f });
    ImGui::SetCursorPosX(18.f);
    ImGui::TextUnformatted("@createtoplay");
    ImGui::PopStyleColor();

    ImGui::Dummy({ 0.f, 14.f });
    ImGui::Separator();
    ImGui::Dummy({ 0.f, 8.f });

    // Nav items
    const char* kNavLabels[] = { "  Home", "  My Games" };
    for (int n = 0; n < 2; n++) {
        const bool sel = (m_homeNav == n);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float itemH = 34.f;
        if (sel) {
            sdl->AddRectFilled(p0, { p0.x + sideW, p0.y + itemH },
                               IM_COL32(0x22, 0x55, 0xFF, 38));
            sdl->AddRectFilled(p0, { p0.x + 3.f, p0.y + itemH },
                               IM_COL32(0x35, 0x6A, 0xFF, 255));
        }
        ImGui::PushStyleColor(ImGuiCol_Button,
            sel ? ImVec4{ 0,0,0,0 } : ImVec4{ 0,0,0,0 });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.22f, 0.55f, 1.f, 0.10f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.22f, 0.55f, 1.f, 0.18f });
        ImGui::PushStyleColor(ImGuiCol_Text,
            sel ? ImVec4{ 0.40f, 0.68f, 1.f, 1.f }
                : ImVec4{ 0.78f, 0.80f, 0.84f, 1.f });
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0.10f, 0.5f });
        if (ImGui::Button(kNavLabels[n], { sideW, itemH }))
            m_homeNav = n;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        ImGui::Dummy({ 0.f, 2.f });
    }

    // Push "Create New Place" to the very bottom
    const float usedY = ImGui::GetCursorPosY();
    const float leftH = winH - usedY - 62.f;
    if (leftH > 0.f) ImGui::Dummy({ 0.f, leftH });

    ImGui::Separator();
    ImGui::Dummy({ 0.f, 10.f });
    ImGui::SetCursorPosX(14.f);
    ImGui::PushStyleColor(ImGuiCol_Button,        kBlue);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBlueH);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kBlueA);
    if (ImGui::Button("+ Create New Place", { sideW - 28.f, 32.f }))
        m_newPlaceOpen = true;
    ImGui::PopStyleColor(3);
    ImGui::Dummy({ 0.f, 10.f });

    ImGui::EndChild();  // sidebar

    // ── Content area ──────────────────────────────────────────────────────────
    ImGui::SameLine(0.f, 0.f);
    ImGui::BeginChild("##HContent", { 0.f, winH }, ImGuiChildFlags_None);

    ImGui::Dummy({ 0.f, 28.f });
    ImGui::SetCursorPosX(28.f);
    if (m_fontTitle) ImGui::PushFont(m_fontTitle);
    ImGui::Text("Welcome back, %s!", username.c_str());
    if (m_fontTitle) ImGui::PopFont();

    ImGui::Dummy({ 0.f, 6.f });
    ImGui::SetCursorPosX(28.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.50f, 0.52f, 0.58f, 1.f });
    ImGui::TextUnformatted(m_homeNav == 0 ? "Recent Places" : "Your Games");
    ImGui::PopStyleColor();
    ImGui::Dummy({ 0.f, 18.f });

    // Empty state
    if (games.empty()) {
        ImGui::SetCursorPosX(28.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.45f, 0.47f, 0.52f, 1.f });
        ImGui::TextUnformatted("You don't have any places yet.");
        ImGui::SetCursorPosX(28.f);
        ImGui::TextUnformatted("Click \"+ Create New Place\" to get started.");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        if (m_newPlaceOpen) { ImGui::OpenPopup("NewPlaceModal"); m_newPlaceOpen = false; }
        ImGui::SetNextWindowPos(
            { vp->WorkPos.x + vp->WorkSize.x * 0.5f,
              vp->WorkPos.y + vp->WorkSize.y * 0.5f },
            ImGuiCond_Always, { 0.5f, 0.5f });
        if (ImGui::BeginPopupModal("NewPlaceModal", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::Dummy({ 0.f, 10.f });
            ImGui::SetCursorPosX(18.f);
            if (m_fontTitle) ImGui::PushFont(m_fontTitle);
            ImGui::TextUnformatted("Create New Place");
            if (m_fontTitle) ImGui::PopFont();
            ImGui::SetCursorPosX(18.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.50f, 0.52f, 0.58f, 1.f });
            ImGui::TextUnformatted("Give your place a name to get started.");
            ImGui::PopStyleColor();
            ImGui::Dummy({ 0.f, 14.f });
            ImGui::SetCursorPosX(18.f);
            ImGui::SetNextItemWidth(320.f);
            ImGui::InputText("##npname", m_newPlaceName, sizeof(m_newPlaceName));
            ImGui::Dummy({ 0.f, 16.f });
            ImGui::SetCursorPosX(18.f);
            ImGui::PushStyleColor(ImGuiCol_Button,        kBlue);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBlueH);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kBlueA);
            const bool dc = ImGui::Button("Create", { 152.f, 34.f });
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0.f, 8.f);
            const bool dcancel = ImGui::Button("Cancel", { 152.f, 34.f });
            ImGui::Dummy({ 0.f, 10.f });
            if (dc) { result = 1; ImGui::CloseCurrentPopup(); }
            if (dcancel) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::End();
        return result;
    }

    // Game card grid
    const float cardW  = 170.f;
    const float cardH  = 196.f;
    const float thumbH = 108.f;
    const float gapX   = 16.f;
    const float gapY   = 16.f;
    const float padX   = 28.f;

    const float availW = ImGui::GetContentRegionAvail().x - padX;
    const int   cols   = std::max(1, (int)((availW + gapX) / (cardW + gapX)));
    const int   rows   = ((int)games.size() + cols - 1) / cols;

    const float gridStartX = ImGui::GetCursorPosX() + padX;
    const float gridStartY = ImGui::GetCursorPosY();

    ImDrawList* cdl = ImGui::GetWindowDrawList();

    for (int i = 0; i < (int)games.size(); i++) {
        const int col = i % cols;
        const int row = i / cols;

        ImGui::SetCursorPos({
            gridStartX + col * (cardW + gapX),
            gridStartY + row * (cardH + gapY)
        });

        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        ImGui::InvisibleButton("##card", { cardW, cardH });
        const bool hov = ImGui::IsItemHovered();
        const bool clk = ImGui::IsItemClicked();
        ImGui::PopID();

        if (clk) result = i + 2;

        // Card background
        cdl->AddRectFilled(cp, { cp.x + cardW, cp.y + cardH },
            hov ? IM_COL32(0x22, 0x22, 0x26, 255)
                : IM_COL32(0x16, 0x16, 0x18, 255),
            10.f);
        cdl->AddRect(cp, { cp.x + cardW, cp.y + cardH },
                     ImGui::GetColorU32(ImGuiCol_Border), 10.f, 0, 1.f);

        // Thumbnail colour block
        const glm::vec3& tc = games[i].thumbnailColor;
        cdl->AddRectFilled(cp, { cp.x + cardW, cp.y + thumbH },
            IM_COL32((int)(tc.r * 255), (int)(tc.g * 255), (int)(tc.b * 255), 255),
            10.f, ImDrawFlags_RoundCornersTop);

        // Game name
        cdl->AddText({ cp.x + 10.f, cp.y + thumbH + 10.f },
                     IM_COL32(218, 220, 226, 255),
                     games[i].name.c_str());

        // Last edited
        cdl->AddText({ cp.x + 10.f, cp.y + thumbH + 30.f },
                     IM_COL32(118, 120, 130, 255),
                     games[i].lastEdited.c_str());

        // "Edit" button that appears on hover
        if (hov) {
            const ImVec2 bMin = { cp.x + 10.f,        cp.y + cardH - 38.f };
            const ImVec2 bMax = { cp.x + cardW - 10.f, cp.y + cardH - 12.f };
            cdl->AddRectFilled(bMin, bMax, IM_COL32(0x35, 0x6A, 0xFF, 228), 6.f);
            const char* et  = "Edit";
            const ImVec2 esz = ImGui::CalcTextSize(et);
            cdl->AddText(
                { (bMin.x + bMax.x - esz.x) * 0.5f,
                  (bMin.y + bMax.y - esz.y) * 0.5f },
                IM_COL32(255, 255, 255, 255), et);
        }
    }

    // Advance cursor past the grid so the child knows its content height.
    if (!games.empty())
        ImGui::SetCursorPos({ 0.f, gridStartY + rows * (cardH + gapY) });
    ImGui::Dummy({ 1.f, 1.f });

    ImGui::EndChild();  // content

    // ── "Create New Place" modal ───────────────────────────────────────────────
    // OpenPopup must be called from the parent window (##Home), not a child.
    if (m_newPlaceOpen) {
        ImGui::OpenPopup("NewPlaceModal");
        m_newPlaceOpen = false;
    }
    ImGui::SetNextWindowPos(
        { vp->WorkPos.x + vp->WorkSize.x * 0.5f,
          vp->WorkPos.y + vp->WorkSize.y * 0.5f },
        ImGuiCond_Always, { 0.5f, 0.5f });
    if (ImGui::BeginPopupModal("NewPlaceModal", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {

        ImGui::Dummy({ 0.f, 10.f });
        ImGui::SetCursorPosX(18.f);
        if (m_fontTitle) ImGui::PushFont(m_fontTitle);
        ImGui::TextUnformatted("Create New Place");
        if (m_fontTitle) ImGui::PopFont();

        ImGui::SetCursorPosX(18.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.50f, 0.52f, 0.58f, 1.f });
        ImGui::TextUnformatted("Give your place a name to get started.");
        ImGui::PopStyleColor();

        ImGui::Dummy({ 0.f, 14.f });
        ImGui::SetCursorPosX(18.f);
        ImGui::SetNextItemWidth(320.f);
        ImGui::InputText("##npname", m_newPlaceName, sizeof(m_newPlaceName));
        ImGui::Dummy({ 0.f, 16.f });

        ImGui::SetCursorPosX(18.f);
        ImGui::PushStyleColor(ImGuiCol_Button,        kBlue);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBlueH);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kBlueA);
        const bool doCreate = ImGui::Button("Create", { 152.f, 34.f });
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, 8.f);
        const bool doCancel = ImGui::Button("Cancel", { 152.f, 34.f });
        ImGui::Dummy({ 0.f, 10.f });

        if (doCreate) { result = 1; ImGui::CloseCurrentPopup(); }
        if (doCancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();  // ##Home
    return result;
}
