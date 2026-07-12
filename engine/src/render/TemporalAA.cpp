#include "engine/render/TemporalAA.h"

#include <cmath>

#include <glm/geometric.hpp>

namespace engine {
namespace {

float Halton(uint64_t index, uint64_t base)
{
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0)
    {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

} // namespace

glm::vec2 TemporalJitterPixels(uint64_t frameIndex)
{
    const uint64_t sample = frameIndex % 8 + 1;
    return {Halton(sample, 2) - 0.5f, Halton(sample, 3) - 0.5f};
}

glm::mat4 ApplyProjectionJitter(const glm::mat4& projection, const glm::vec2& jitterPixels,
                                int width, int height, float scale)
{
    glm::mat4 jittered = projection;
    if (width > 0 && height > 0)
    {
        jittered[2][0] += jitterPixels.x * scale * 2.0f / static_cast<float>(width);
        jittered[2][1] += jitterPixels.y * scale * 2.0f / static_cast<float>(height);
    }
    return jittered;
}

bool IsTemporalCameraCut(const glm::vec3& previousPosition, const glm::vec3& currentPosition,
                         const glm::vec3& previousForward, const glm::vec3& currentForward,
                         float previousFovDegrees, float currentFovDegrees)
{
    if (glm::distance(previousPosition, currentPosition) > 5.0f)
        return true;
    if (glm::dot(glm::normalize(previousForward), glm::normalize(currentForward)) < 0.65f)
        return true;
    return std::abs(previousFovDegrees - currentFovDegrees) > 5.0f;
}

} // namespace engine
