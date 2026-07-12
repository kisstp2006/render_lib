#pragma once

#include <array>

#include <glm/glm.hpp>

namespace engine {

class Camera;

constexpr int kShadowCascadeCount = 4;

struct CascadeShadowConfig
{
    float MaxDistance = 150.0f;
    float SplitLambda = 0.65f;
    float BlendFraction = 0.10f;
    float DepthPadding = 25.0f;
    std::array<int, kShadowCascadeCount> Resolutions{2048, 2048, 1024, 1024};
};

struct CascadeShadowData
{
    std::array<glm::mat4, kShadowCascadeCount> LightMatrices{};
    std::array<float, kShadowCascadeCount> SplitDepths{};
    std::array<float, kShadowCascadeCount> NearDepths{};
    std::array<float, kShadowCascadeCount> WorldUnitsPerTexel{};
};

std::array<float, kShadowCascadeCount> CalculateCascadeSplits(float nearPlane, float farPlane, float lambda);
CascadeShadowData BuildCascadeShadows(const Camera& camera, float aspectRatio, const glm::vec3& lightDirection,
                                      const CascadeShadowConfig& config = {});

} // namespace engine
