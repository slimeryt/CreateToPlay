#pragma once
#include "BasePart.h"
#include <vector>
#include <memory>
#include <string>

class Renderer;

class Workspace {
public:
    BasePart* AddPart(std::unique_ptr<BasePart> part);
    void UpdateAll();
    void RenderAll(Renderer& renderer) const;

    const std::vector<std::unique_ptr<BasePart>>& GetParts() const { return m_parts; }

private:
    std::vector<std::unique_ptr<BasePart>> m_parts;
};
