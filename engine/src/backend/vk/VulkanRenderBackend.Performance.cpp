#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/core/Log.h"

#include <array>
#include <string>
#include <vector>

namespace engine {

void VulkanRenderBackend::CreatePerformanceQueries()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    m_timestampPeriodNanoseconds = properties.limits.timestampPeriod;

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, families.data());
    const QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    const bool timestampSupported = indices.Graphics
        && families[*indices.Graphics].timestampValidBits > 0
        && properties.limits.timestampComputeAndGraphics == VK_TRUE;
    m_capabilities.GpuTiming = timestampSupported;
    m_gpuTimingSupported = timestampSupported && m_config.EnableGpuTiming;
    if (timestampSupported && !m_config.EnableGpuTiming)
        return;
    if (!m_gpuTimingSupported)
    {
        log::Warn("Vulkan GPU timestamp queries are not supported by the selected graphics queue");
        return;
    }

    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = kFramesInFlight * kTimestampCountPerFrame;
    if (vkCreateQueryPool(m_device, &queryInfo, nullptr, &m_timestampQueryPool) != VK_SUCCESS)
    {
        m_gpuTimingSupported = false;
        log::Warn("Vulkan GPU timestamp query pool creation failed; profiling is disabled");
    }
}

void VulkanRenderBackend::DestroyPerformanceQueries()
{
    if (m_timestampQueryPool != VK_NULL_HANDLE)
        vkDestroyQueryPool(m_device, m_timestampQueryPool, nullptr);
    m_timestampQueryPool = VK_NULL_HANDLE;
    m_gpuTimingSupported = false;
    m_timestampFrameWritten.fill(false);
    m_shadowTiming.Reset();
    m_mainTiming.Reset();
    m_postTiming.Reset();
}

void VulkanRenderBackend::ReadPerformanceQueries(uint32_t frameIndex)
{
    if (!m_gpuTimingSupported || !m_timestampFrameWritten[frameIndex])
        return;

    std::array<uint64_t, kTimestampCountPerFrame> timestamps{};
    const uint32_t firstQuery = frameIndex * kTimestampCountPerFrame;
    const VkResult result = vkGetQueryPoolResults(
        m_device, m_timestampQueryPool, firstQuery, kTimestampCountPerFrame,
        sizeof(timestamps), timestamps.data(), sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS)
        return;

    const auto milliseconds = [&](uint32_t begin, uint32_t end) {
        const uint64_t ticks = timestamps[end] >= timestamps[begin]
            ? timestamps[end] - timestamps[begin] : 0;
        return static_cast<float>(static_cast<double>(ticks)
            * static_cast<double>(m_timestampPeriodNanoseconds) / 1'000'000.0);
    };

    m_frameStats.GpuShadowMilliseconds = milliseconds(0, 1);
    m_frameStats.GpuMainMilliseconds = milliseconds(2, 3);
    m_frameStats.GpuPostMilliseconds = milliseconds(4, 5);
    m_frameStats.GpuFrameMilliseconds = m_frameStats.GpuShadowMilliseconds
                                      + m_frameStats.GpuMainMilliseconds
                                      + m_frameStats.GpuPostMilliseconds;
    m_frameStats.GpuTimingAvailable = true;

    if (m_timestampLogShadows[frameIndex])
    {
        if (const auto summary = m_shadowTiming.Submit(milliseconds(0, 1)))
            log::Info(FormatGpuTiming("Vulkan shadows GPU", *summary));
    }
    if (m_timestampLogPost[frameIndex])
    {
        if (const auto summary = m_mainTiming.Submit(milliseconds(2, 3)))
            log::Info(FormatGpuTiming("Vulkan main HDR GPU", *summary));
        if (const auto summary = m_postTiming.Submit(milliseconds(4, 5)))
        {
            const AntiAliasingMode mode = m_timestampAaMode[frameIndex];
            const char* modeName = mode == AntiAliasingMode::Taa ? "TAA"
                                 : mode == AntiAliasingMode::Fxaa ? "FXAA" : "NONE";
            log::Info(FormatGpuTiming(std::string("Vulkan post GPU (") + modeName + ")",
                                      *summary));
        }
    }
    m_timestampFrameWritten[frameIndex] = false;
}

} // namespace engine
