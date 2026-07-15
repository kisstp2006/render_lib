#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace engine
{
class Camera;
class Scene;

struct PickingRay
{
    glm::vec3 Origin{0.0f};
    glm::vec3 Direction{0.0f, 0.0f, -1.0f};
};

struct PickingResult
{
    bool Hit = false;
    uint64_t Entity = 0;
    uint32_t InstanceIndex = 0;
    float Distance = 0.0f;
    glm::vec3 Position{0.0f};
};

PickingRay BuildPickingRay(const Camera& camera, float pixelX, float pixelY,
                           uint32_t viewportWidth, uint32_t viewportHeight);
PickingResult PickScene(const Scene& scene, const Camera& camera,
                        float pixelX, float pixelY,
                        uint32_t viewportWidth, uint32_t viewportHeight);
void ApplyPickingSelection(Scene& scene, const PickingResult& result);

} // namespace engine
