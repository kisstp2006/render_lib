#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace engine {

// Centered Halton(2,3) sample in pixel units, repeated over an 8-frame cycle.
glm::vec2 TemporalJitterPixels(uint64_t frameIndex);

glm::mat4 ApplyProjectionJitter(const glm::mat4& projection, const glm::vec2& jitterPixels,
                                int width, int height, float scale = 1.0f);

bool IsTemporalCameraCut(const glm::vec3& previousPosition, const glm::vec3& currentPosition,
                         const glm::vec3& previousForward, const glm::vec3& currentForward,
                         float previousFovDegrees, float currentFovDegrees);

} // namespace engine
