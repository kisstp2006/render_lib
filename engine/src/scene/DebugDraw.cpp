#include "engine/scene/DebugDraw.h"

#include <algorithm>
#include <cmath>

namespace engine
{

void DebugDrawList::Clear()
{
    m_lines.clear();
    m_icons.clear();
}

void DebugDrawList::Line(const glm::vec3& start, const glm::vec3& end,
                         const glm::vec3& color, DebugDepthMode depth)
{
    m_lines.push_back({start, end, color, depth});
}

void DebugDrawList::Aabb(const glm::vec3& minimum, const glm::vec3& maximum,
                         const glm::vec3& color, DebugDepthMode depth)
{
    const glm::vec3 c[] = {
        {minimum.x, minimum.y, minimum.z}, {maximum.x, minimum.y, minimum.z},
        {maximum.x, maximum.y, minimum.z}, {minimum.x, maximum.y, minimum.z},
        {minimum.x, minimum.y, maximum.z}, {maximum.x, minimum.y, maximum.z},
        {maximum.x, maximum.y, maximum.z}, {minimum.x, maximum.y, maximum.z}};
    constexpr uint8_t edges[][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}};
    for (const auto& edge : edges)
        Line(c[edge[0]], c[edge[1]], color, depth);
}

void DebugDrawList::Grid(float halfExtent, float spacing,
                         const glm::vec3& origin, DebugDepthMode depth)
{
    halfExtent = std::max(halfExtent, 0.0f);
    spacing = std::max(spacing, 0.001f);
    const int count = static_cast<int>(std::floor(halfExtent / spacing));
    for (int index = -count; index <= count; ++index)
    {
        const float offset = static_cast<float>(index) * spacing;
        const glm::vec3 color = index == 0 ? glm::vec3(0.34f, 0.42f, 0.55f)
                                           : glm::vec3(0.12f, 0.15f, 0.20f);
        Line(origin + glm::vec3(-halfExtent, 0.0f, offset),
             origin + glm::vec3(halfExtent, 0.0f, offset), color, depth);
        Line(origin + glm::vec3(offset, 0.0f, -halfExtent),
             origin + glm::vec3(offset, 0.0f, halfExtent), color, depth);
    }
}

void DebugDrawList::Icon(DebugIconType type, const glm::vec3& position,
                         const glm::vec3& color, float size,
                         DebugDepthMode depth)
{
    m_icons.push_back({position, color, std::max(size, 0.001f), type, depth});
}

} // namespace engine
