#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace engine
{

enum class DebugDepthMode : uint8_t
{
    DepthTested,
    Overlay
};

enum class DebugIconType : uint8_t
{
    PointLight,
    SpotLight,
    DirectionalLight,
    Camera,
    Generic
};

struct DebugLine
{
    glm::vec3 Start{0.0f};
    glm::vec3 End{0.0f};
    glm::vec3 Color{1.0f};
    DebugDepthMode Depth = DebugDepthMode::DepthTested;
};

struct DebugIcon
{
    glm::vec3 Position{0.0f};
    glm::vec3 Color{1.0f};
    float Size = 0.35f;
    DebugIconType Type = DebugIconType::Generic;
    DebugDepthMode Depth = DebugDepthMode::Overlay;
};

class DebugDrawList
{
public:
    void Clear();
    void Line(const glm::vec3& start, const glm::vec3& end,
              const glm::vec3& color = glm::vec3(1.0f),
              DebugDepthMode depth = DebugDepthMode::DepthTested);
    void Aabb(const glm::vec3& minimum, const glm::vec3& maximum,
              const glm::vec3& color = glm::vec3(1.0f),
              DebugDepthMode depth = DebugDepthMode::DepthTested);
    void Grid(float halfExtent = 10.0f, float spacing = 1.0f,
              const glm::vec3& origin = glm::vec3(0.0f),
              DebugDepthMode depth = DebugDepthMode::DepthTested);
    void Icon(DebugIconType type, const glm::vec3& position,
              const glm::vec3& color = glm::vec3(1.0f), float size = 0.35f,
              DebugDepthMode depth = DebugDepthMode::Overlay);

    const std::vector<DebugLine>& Lines() const noexcept { return m_lines; }
    const std::vector<DebugIcon>& Icons() const noexcept { return m_icons; }

private:
    std::vector<DebugLine> m_lines;
    std::vector<DebugIcon> m_icons;
};

} // namespace engine
