#include "engine/render/Picking.h"

#include "engine/core/Camera.h"
#include "engine/render/Visibility.h"
#include "engine/scene/Scene.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine
{
namespace
{

bool IntersectAabb(const PickingRay& ray, const AxisAlignedBounds& bounds,
                   float& distance)
{
    float nearDistance = 0.0f;
    float farDistance = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis)
    {
        const float direction = ray.Direction[axis];
        if (std::abs(direction) < 1.0e-7f)
        {
            if (ray.Origin[axis] < bounds.Minimum[axis] ||
                ray.Origin[axis] > bounds.Maximum[axis])
                return false;
            continue;
        }
        const float inverse = 1.0f / direction;
        float first = (bounds.Minimum[axis] - ray.Origin[axis]) * inverse;
        float second = (bounds.Maximum[axis] - ray.Origin[axis]) * inverse;
        if (first > second)
            std::swap(first, second);
        nearDistance = std::max(nearDistance, first);
        farDistance = std::min(farDistance, second);
        if (nearDistance > farDistance)
            return false;
    }
    distance = nearDistance;
    return farDistance >= 0.0f;
}

} // namespace

PickingRay BuildPickingRay(const Camera& camera, float pixelX, float pixelY,
                           uint32_t viewportWidth, uint32_t viewportHeight)
{
    if (viewportWidth == 0 || viewportHeight == 0)
        return {camera.Position, camera.Forward()};
    const float x = 2.0f * pixelX / static_cast<float>(viewportWidth) - 1.0f;
    const float y = 1.0f - 2.0f * pixelY / static_cast<float>(viewportHeight);
    const float aspect = static_cast<float>(viewportWidth) /
                         static_cast<float>(viewportHeight);
    const glm::mat4 inverse = glm::inverse(
        camera.GetProjection(aspect) * camera.GetView());
    glm::vec4 nearPoint = inverse * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 farPoint = inverse * glm::vec4(x, y, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    const glm::vec3 origin = camera.Projection == CameraProjection::Orthographic
        ? glm::vec3(nearPoint) : camera.Position;
    return {origin, glm::normalize(glm::vec3(farPoint - nearPoint))};
}

PickingResult PickScene(const Scene& scene, const Camera& camera,
                        float pixelX, float pixelY,
                        uint32_t viewportWidth, uint32_t viewportHeight)
{
    const PickingRay ray = BuildPickingRay(camera, pixelX, pixelY,
                                           viewportWidth, viewportHeight);
    PickingResult closest;
    closest.Distance = std::numeric_limits<float>::max();
    for (size_t index = 0; index < scene.Instances().size(); ++index)
    {
        const MeshInstance& instance = scene.Instances()[index];
        if (!instance.Mesh || instance.SourceEntity == 0)
            continue;
        const AxisAlignedBounds local = ComputeMeshBounds(*instance.Mesh);
        const AxisAlignedBounds world = TransformBounds(local, instance.Transform);
        float distance = 0.0f;
        if (!world.Valid || !IntersectAabb(ray, world, distance) ||
            distance >= closest.Distance)
            continue;
        closest.Hit = true;
        closest.Entity = instance.SourceEntity;
        closest.InstanceIndex = static_cast<uint32_t>(index);
        closest.Distance = distance;
        closest.Position = ray.Origin + ray.Direction * distance;
    }
    if (!closest.Hit)
        closest.Distance = 0.0f;
    return closest;
}

void ApplyPickingSelection(Scene& scene, const PickingResult& result)
{
    scene.SelectedEntity = result.Hit ? result.Entity : 0;
}

} // namespace engine
