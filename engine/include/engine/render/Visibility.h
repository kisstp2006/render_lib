#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace engine
{

struct MeshData;

struct AxisAlignedBounds
{
    glm::vec3 Minimum{0.0f};
    glm::vec3 Maximum{0.0f};
    bool Valid = false;

    [[nodiscard]] glm::vec3 Center() const noexcept
    {
        return (Minimum + Maximum) * 0.5f;
    }
    [[nodiscard]] glm::vec3 Extents() const noexcept
    {
        return (Maximum - Minimum) * 0.5f;
    }
};

struct ViewFrustum
{
    // Plane equation: dot(xyz, worldPosition) + w >= 0 is inside.
    std::array<glm::vec4, 6> Planes{};
};

enum class VisibilityClassification : uint8_t
{
    Visible,
    FrustumCulled,
    DistanceCulled,
    InvalidBounds
};

struct VisibilityDebugBounds
{
    AxisAlignedBounds Bounds;
    VisibilityClassification Classification = VisibilityClassification::Visible;
    uint32_t InstanceIndex = 0;
};

struct VisibilityStatistics
{
    uint32_t Tested = 0;
    uint32_t Visible = 0;
    uint32_t ShadowCasters = 0;
    uint32_t FrustumCulled = 0;
    uint32_t DistanceCulled = 0;
    uint32_t InvalidBounds = 0;
};

AxisAlignedBounds ComputeMeshBounds(const MeshData& mesh);
AxisAlignedBounds TransformBounds(const AxisAlignedBounds& localBounds,
                                  const glm::mat4& transform);
ViewFrustum ExtractViewFrustum(const glm::mat4& viewProjection);
bool IntersectsFrustum(const ViewFrustum& frustum, const AxisAlignedBounds& bounds);
float SquaredDistanceToBounds(const glm::vec3& position,
                              const AxisAlignedBounds& bounds);

} // namespace engine
