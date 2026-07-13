#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace engine {

struct GpuTimingSummary
{
    float AverageMilliseconds = 0.0f;
    float MinimumMilliseconds = 0.0f;
    float MaximumMilliseconds = 0.0f;
    uint32_t SampleCount = 0;
};

// API-neutral timing aggregation. OpenGL and Vulkan own their native query
// objects, but warm-up, min/average/max collection and log formatting should
// behave identically.
class GpuTimingAccumulator
{
public:
    std::optional<GpuTimingSummary> Submit(float milliseconds,
                                           uint32_t warmupFrames = 10,
                                           uint32_t reportInterval = 120);
    void Reset();

private:
    uint64_t m_frameCount = 0;
    float m_totalMilliseconds = 0.0f;
    float m_minimumMilliseconds = std::numeric_limits<float>::max();
    float m_maximumMilliseconds = 0.0f;
    uint32_t m_sampleCount = 0;
};

std::string FormatGpuTiming(std::string_view label,
                            const GpuTimingSummary& summary);

} // namespace engine
