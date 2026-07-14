#include "engine/render/Visibility.h"

#include "engine/scene/Mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine
{

AxisAlignedBounds ComputeMeshBounds(const MeshData& mesh)
{
    AxisAlignedBounds bounds;
    if (mesh.Vertices.empty())
        return bounds;
    bounds.Minimum = glm::vec3(std::numeric_limits<float>::max());
    bounds.Maximum = glm::vec3(std::numeric_limits<float>::lowest());
    for (const Vertex& vertex : mesh.Vertices)
    {
        if (!std::isfinite(vertex.Position.x) || !std::isfinite(vertex.Position.y) ||
            !std::isfinite(vertex.Position.z))
            return {};
        bounds.Minimum = glm::min(bounds.Minimum, vertex.Position);
        bounds.Maximum = glm::max(bounds.Maximum, vertex.Position);
    }
    bounds.Valid = true;
    return bounds;
}

AxisAlignedBounds TransformBounds(const AxisAlignedBounds& localBounds,
                                  const glm::mat4& transform)
{
    if (!localBounds.Valid)
        return {};
    const glm::vec3 localCenter = localBounds.Center();
    const glm::vec3 localExtents = localBounds.Extents();
    const glm::vec3 worldCenter = glm::vec3(transform * glm::vec4(localCenter, 1.0f));
    const glm::mat3 linear(transform);
    // GLM matrices are column-major: abs(M) * extents is assembled by columns.
    const glm::vec3 transformedExtents =
        glm::abs(linear[0]) * localExtents.x +
        glm::abs(linear[1]) * localExtents.y +
        glm::abs(linear[2]) * localExtents.z;
    AxisAlignedBounds result;
    result.Minimum = worldCenter - transformedExtents;
    result.Maximum = worldCenter + transformedExtents;
    result.Valid = std::isfinite(worldCenter.x) && std::isfinite(worldCenter.y) &&
                   std::isfinite(worldCenter.z) && std::isfinite(transformedExtents.x) &&
                   std::isfinite(transformedExtents.y) && std::isfinite(transformedExtents.z);
    return result.Valid ? result : AxisAlignedBounds{};
}

ViewFrustum ExtractViewFrustum(const glm::mat4& viewProjection)
{
    const glm::mat4 rows = glm::transpose(viewProjection);
    ViewFrustum result;
    result.Planes[0] = rows[3] + rows[0]; // left
    result.Planes[1] = rows[3] - rows[0]; // right
    result.Planes[2] = rows[3] + rows[1]; // bottom
    result.Planes[3] = rows[3] - rows[1]; // top
    result.Planes[4] = rows[3] + rows[2]; // near (OpenGL/GLM -1..1 depth)
    result.Planes[5] = rows[3] - rows[2]; // far
    for (glm::vec4& plane : result.Planes)
    {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1.0e-8f)
            plane /= length;
    }
    return result;
}

bool IntersectsFrustum(const ViewFrustum& frustum, const AxisAlignedBounds& bounds)
{
    if (!bounds.Valid)
        return true;
    for (const glm::vec4& plane : frustum.Planes)
    {
        const glm::vec3 positive{
            plane.x >= 0.0f ? bounds.Maximum.x : bounds.Minimum.x,
            plane.y >= 0.0f ? bounds.Maximum.y : bounds.Minimum.y,
            plane.z >= 0.0f ? bounds.Maximum.z : bounds.Minimum.z};
        if (glm::dot(glm::vec3(plane), positive) + plane.w < 0.0f)
            return false;
    }
    return true;
}

float SquaredDistanceToBounds(const glm::vec3& position,
                              const AxisAlignedBounds& bounds)
{
    if (!bounds.Valid)
        return 0.0f;
    const glm::vec3 nearest = glm::clamp(position, bounds.Minimum, bounds.Maximum);
    const glm::vec3 delta = position - nearest;
    return glm::dot(delta, delta);
}

} // namespace engine
