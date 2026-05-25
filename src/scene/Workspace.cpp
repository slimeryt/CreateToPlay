#include "Workspace.h"
#include "renderer/Renderer.h"

BasePart* Workspace::AddPart(std::unique_ptr<BasePart> part) {
    m_parts.push_back(std::move(part));
    return m_parts.back().get();
}

void Workspace::UpdateAll() {
    for (auto& p : m_parts)
        p->SyncFromPhysics();
}

void Workspace::RenderAll(Renderer& renderer) const {
    for (auto& p : m_parts)
        renderer.DrawPart(*p);
}
