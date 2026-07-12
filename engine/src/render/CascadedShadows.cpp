#include "engine/render/CascadedShadows.h"

#include "engine/core/Camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace engine {

std::array<float, kShadowCascadeCount> CalculateCascadeSplits(float nearPlane, float farPlane, float lambda)
{
    std::array<float, kShadowCascadeCount> splits{};
    nearPlane = std::max(nearPlane, 0.001f);
    farPlane = std::max(farPlane, nearPlane + 0.001f);
    lambda = glm::clamp(lambda, 0.0f, 1.0f);
    for (int i = 1; i <= kShadowCascadeCount; ++i)
    {
        const float fraction = static_cast<float>(i) / kShadowCascadeCount;
        const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, fraction);
        const float linear = nearPlane + (farPlane - nearPlane) * fraction;
        splits[i - 1] = glm::mix(linear, logarithmic, lambda);
    }
    splits.back() = farPlane;
    return splits;
}

CascadeShadowData BuildCascadeShadows(const Camera& camera, float aspectRatio, const glm::vec3& lightDirection,
                                      const CascadeShadowConfig& config)
{
    CascadeShadowData result;
    const float shadowFar = std::min(camera.FarPlane, std::max(config.MaxDistance, camera.NearPlane + 0.001f));
    result.SplitDepths = CalculateCascadeSplits(camera.NearPlane, shadowFar, config.SplitLambda);

    const glm::vec3 forward = camera.Forward();
    const glm::vec3 right = camera.Right();
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const glm::vec3 lightDir = glm::normalize(lightDirection);
    const glm::vec3 lightUpReference = std::abs(lightDir.y) > 0.98f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 lightRight = glm::normalize(glm::cross(lightDir, lightUpReference));
    const glm::vec3 lightUp = glm::normalize(glm::cross(lightRight, lightDir));
    const float tanHalfFov = std::tan(glm::radians(camera.FovDegrees) * 0.5f);

    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade)
    {
        const float nominalNear = cascade == 0 ? camera.NearPlane : result.SplitDepths[cascade - 1];
        const float previousNear = cascade <= 1 ? camera.NearPlane : result.SplitDepths[cascade - 2];
        const float overlap = cascade == 0 ? 0.0f
            : (nominalNear - previousNear) * glm::clamp(config.BlendFraction, 0.0f, 0.5f);
        const float cascadeNear = nominalNear - overlap;
        const float cascadeFar = result.SplitDepths[cascade];
        result.NearDepths[cascade] = cascadeNear;
        const float nearHeight = tanHalfFov * cascadeNear;
        const float nearWidth = nearHeight * aspectRatio;
        const float farHeight = tanHalfFov * cascadeFar;
        const float farWidth = farHeight * aspectRatio;
        const glm::vec3 nearCenter = camera.Position + forward * cascadeNear;
        const glm::vec3 farCenter = camera.Position + forward * cascadeFar;

        const std::array<glm::vec3, 8> corners{
            nearCenter - right * nearWidth - up * nearHeight,
            nearCenter + right * nearWidth - up * nearHeight,
            nearCenter + right * nearWidth + up * nearHeight,
            nearCenter - right * nearWidth + up * nearHeight,
            farCenter - right * farWidth - up * farHeight,
            farCenter + right * farWidth - up * farHeight,
            farCenter + right * farWidth + up * farHeight,
            farCenter - right * farWidth + up * farHeight,
        };

        glm::vec3 center(0.0f);
        for (const glm::vec3& corner : corners)
            center += corner;
        center /= static_cast<float>(corners.size());

        float radius = 0.0f;
        for (const glm::vec3& corner : corners)
            radius = std::max(radius, glm::length(corner - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const float worldUnitsPerTexel = (2.0f * radius) / static_cast<float>(config.Resolutions[cascade]);
        result.WorldUnitsPerTexel[cascade] = worldUnitsPerTexel;
        const float centerRight = std::floor(glm::dot(center, lightRight) / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
        const float centerUp = std::floor(glm::dot(center, lightUp) / worldUnitsPerTexel + 0.5f) * worldUnitsPerTexel;
        center += lightRight * (centerRight - glm::dot(center, lightRight));
        center += lightUp * (centerUp - glm::dot(center, lightUp));

        const float eyeDistance = radius + config.DepthPadding;
        const glm::mat4 lightView = glm::lookAt(center - lightDir * eyeDistance, center, lightUp);
        float minZ = 1e30f;
        float maxZ = -1e30f;
        for (const glm::vec3& corner : corners)
        {
            const float z = (lightView * glm::vec4(corner, 1.0f)).z;
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
        const float nearClip = std::max(0.1f, -maxZ - config.DepthPadding);
        const float farClip = std::max(nearClip + 0.1f, -minZ + config.DepthPadding);
        const glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, nearClip, farClip);
        result.LightMatrices[cascade] = lightProjection * lightView;
    }
    return result;
}

} // namespace engine
