#include "engine/render/GpuTiming.h"

#include <algorithm>

namespace engine {

std::optional<GpuTimingSummary> GpuTimingAccumulator::Submit(
    float milliseconds, uint32_t warmupFrames, uint32_t reportInterval)
{
    ++m_frameCount;
    if (m_frameCount > warmupFrames)
    {
        m_totalMilliseconds += milliseconds;
        m_minimumMilliseconds = std::min(m_minimumMilliseconds, milliseconds);
        m_maximumMilliseconds = std::max(m_maximumMilliseconds, milliseconds);
        ++m_sampleCount;
    }

    if (reportInterval == 0 || m_frameCount % reportInterval != 0
        || m_sampleCount == 0)
    {
        return std::nullopt;
    }

    GpuTimingSummary summary;
    summary.AverageMilliseconds = m_totalMilliseconds / static_cast<float>(m_sampleCount);
    summary.MinimumMilliseconds = m_minimumMilliseconds;
    summary.MaximumMilliseconds = m_maximumMilliseconds;
    summary.SampleCount = m_sampleCount;

    m_totalMilliseconds = 0.0f;
    m_minimumMilliseconds = std::numeric_limits<float>::max();
    m_maximumMilliseconds = 0.0f;
    m_sampleCount = 0;
    return summary;
}

void GpuTimingAccumulator::Reset()
{
    m_frameCount = 0;
    m_totalMilliseconds = 0.0f;
    m_minimumMilliseconds = std::numeric_limits<float>::max();
    m_maximumMilliseconds = 0.0f;
    m_sampleCount = 0;
}

std::string FormatGpuTiming(std::string_view label,
                            const GpuTimingSummary& summary)
{
    return std::string(label) + ": avg " + std::to_string(summary.AverageMilliseconds)
         + " ms, min " + std::to_string(summary.MinimumMilliseconds)
         + " ms, max " + std::to_string(summary.MaximumMilliseconds)
         + " ms (" + std::to_string(summary.SampleCount) + " samples)";
}

} // namespace engine
