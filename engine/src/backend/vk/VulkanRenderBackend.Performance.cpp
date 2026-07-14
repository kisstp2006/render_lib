#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/core/Log.h"

#include <array>
#include <string>
#include <vector>

namespace engine {
namespace {

constexpr std::array<const char*, 5> kPassNames{
    "Shadows/Directional",
    "Shadows/Local lights",
    "Main HDR",
    "Post process",
    "Debug UI"
};

constexpr VkQueryPipelineStatisticFlags kPipelineStatistics =
    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
    VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
    VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
    VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;

} // namespace

BackendResourceStats VulkanRenderBackend::GetResourceStats() const
{
    const vulkan::ResourceMemoryStats memory = m_resources.MemoryStats();
    BackendResourceStats stats;
    stats.DeviceLocalBytes = memory.DeviceLocalBytes;
    stats.PeakDeviceLocalBytes = memory.PeakDeviceLocalBytes;
    stats.HostVisibleBytes = memory.HostVisibleBytes;
    stats.PeakHostVisibleBytes = memory.PeakHostVisibleBytes;
    stats.LiveNativeAllocations = memory.AllocationCount;
    stats.MeshResources = m_meshCache.size();
    stats.TextureResources = m_textureCache.size() + m_panoramaCache.size() +
                             m_colorLutCache.size();
    stats.MaterialResources = m_materialCache.size();
    return stats;
}

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
    m_gpuTimingSupported = timestampSupported &&
        m_capabilities.Gpu.Uses(GpuFeature::GpuTimestamps);
    if (!m_capabilities.Gpu.Uses(GpuFeature::GpuTimestamps))
        return;

    if (m_gpuTimingSupported)
    {
        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = kFramesInFlight * kTimestampCountPerFrame;
        if (vkCreateQueryPool(m_device, &queryInfo, nullptr, &m_timestampQueryPool) != VK_SUCCESS)
        {
            m_gpuTimingSupported = false;
            m_capabilities.GpuTiming = false;
            log::Warn("Vulkan GPU timestamp query pool creation failed");
        }
        else
            SetDebugName(VK_OBJECT_TYPE_QUERY_POOL,
                         reinterpret_cast<uint64_t>(m_timestampQueryPool),
                         "GPU Profiler Timestamp Queries");
    }
    else
        log::Warn("Vulkan GPU timestamps are unsupported by the selected graphics queue");

    if (m_pipelineStatisticsSupported)
    {
        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        queryInfo.queryCount = kFramesInFlight;
        queryInfo.pipelineStatistics = kPipelineStatistics;
        if (vkCreateQueryPool(m_device, &queryInfo, nullptr,
                              &m_pipelineStatisticsQueryPool) != VK_SUCCESS)
        {
            m_pipelineStatisticsSupported = false;
            m_capabilities.GpuPipelineStatistics = false;
            log::Warn("Vulkan pipeline-statistics query pool creation failed");
        }
        else
            SetDebugName(VK_OBJECT_TYPE_QUERY_POOL,
                         reinterpret_cast<uint64_t>(m_pipelineStatisticsQueryPool),
                         "GPU Profiler Pipeline Statistics");
    }
}

void VulkanRenderBackend::DestroyPerformanceQueries()
{
    if (m_timestampQueryPool != VK_NULL_HANDLE)
        vkDestroyQueryPool(m_device, m_timestampQueryPool, nullptr);
    if (m_pipelineStatisticsQueryPool != VK_NULL_HANDLE)
        vkDestroyQueryPool(m_device, m_pipelineStatisticsQueryPool, nullptr);
    m_timestampQueryPool = VK_NULL_HANDLE;
    m_pipelineStatisticsQueryPool = VK_NULL_HANDLE;
    m_gpuTimingSupported = false;
    m_timestampFrameWritten.fill(false);
    m_pipelineStatisticsFrameWritten.fill(false);
    m_shadowTiming.Reset();
    m_localShadowTiming.Reset();
    m_mainTiming.Reset();
    m_postTiming.Reset();
}

profiling::GpuMemoryStatistics VulkanRenderBackend::QueryGpuMemory() const
{
    profiling::GpuMemoryStatistics memory;
    const vulkan::ResourceMemoryStats owned = m_resources.MemoryStats();
    memory.EngineOwnedBytes = owned.DeviceLocalBytes;
    if (!m_memoryBudgetSupported)
    {
        memory.UsageBytes = owned.DeviceLocalBytes;
        memory.BudgetBytes = m_capabilities.DedicatedVideoMemoryBytes;
        memory.AvailableBytes = memory.BudgetBytes > memory.UsageBytes
            ? memory.BudgetBytes - memory.UsageBytes : 0;
        return memory;
    }

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    VkPhysicalDeviceMemoryProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    properties.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(m_physicalDevice, &properties);
    for (uint32_t heap = 0; heap < properties.memoryProperties.memoryHeapCount; ++heap)
    {
        if ((properties.memoryProperties.memoryHeaps[heap].flags &
             VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0)
            continue;
        memory.UsageBytes += budget.heapUsage[heap];
        memory.BudgetBytes += budget.heapBudget[heap];
    }
    memory.AvailableBytes = memory.BudgetBytes > memory.UsageBytes
        ? memory.BudgetBytes - memory.UsageBytes : 0;
    return memory;
}

void VulkanRenderBackend::ReadPerformanceQueries(uint32_t frameIndex)
{
    const bool timingWritten = m_gpuTimingSupported && m_timestampFrameWritten[frameIndex];
    const bool pipelineWritten = m_pipelineStatisticsSupported &&
                                 m_pipelineStatisticsFrameWritten[frameIndex];
    if (!timingWritten && !pipelineWritten)
        return;

    profiling::GpuFrameProfile profile;
    profile.FrameIndex = m_gpuProfileFrameIds[frameIndex];
    profile.BackendName = Name();
    profile.AdapterName = m_capabilities.AdapterName;
    profile.Pipeline.DrawCalls = m_gpuProfileDrawCalls[frameIndex];
    profile.Pipeline.ComputeDispatches = m_gpuProfileDispatches[frameIndex];
    profile.MemoryBudgetAvailable = m_memoryBudgetSupported;
    profile.Memory = QueryGpuMemory();

    std::array<float, GpuProfilerPassCount> passMilliseconds{};
    if (timingWritten)
    {
        std::array<uint64_t, kTimestampCountPerFrame> timestamps{};
        const uint32_t firstQuery = frameIndex * kTimestampCountPerFrame;
        const VkResult result = vkGetQueryPoolResults(
            m_device, m_timestampQueryPool, firstQuery, kTimestampCountPerFrame,
            sizeof(timestamps), timestamps.data(), sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (result == VK_SUCCESS)
        {
            profile.TimingAvailable = true;
            profile.Passes.reserve(GpuProfilerPassCount);
            for (uint32_t pass = 0; pass < GpuProfilerPassCount; ++pass)
            {
                const uint64_t begin = timestamps[pass * 2];
                const uint64_t end = timestamps[pass * 2 + 1];
                const uint64_t ticks = end >= begin ? end - begin : 0;
                passMilliseconds[pass] = static_cast<float>(static_cast<double>(ticks) *
                    static_cast<double>(m_timestampPeriodNanoseconds) / 1'000'000.0);
                profile.Passes.push_back({kPassNames[pass], passMilliseconds[pass]});
                profile.FrameMilliseconds += passMilliseconds[pass];
            }
        }
    }

    if (pipelineWritten)
    {
        std::array<uint64_t, kPipelineCounterCount> counters{};
        const VkResult result = vkGetQueryPoolResults(
            m_device, m_pipelineStatisticsQueryPool, frameIndex, 1,
            sizeof(counters), counters.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (result == VK_SUCCESS)
        {
            profile.PipelineStatisticsAvailable = true;
            profile.Pipeline.InputAssemblyVertices = counters[0];
            profile.Pipeline.InputAssemblyPrimitives = counters[1];
            profile.Pipeline.VertexShaderInvocations = counters[2];
            profile.Pipeline.FragmentShaderInvocations = counters[3];
            profile.Pipeline.ComputeShaderInvocations = counters[4];
        }
    }

    profiling::GpuProfiler::Get().SubmitFrame(profile);
    if (profile.TimingAvailable)
    {
        m_frameStats.GpuTimingAvailable = true;
        m_frameStats.GpuShadowMilliseconds = passMilliseconds[DirectionalShadowPass]
                                           + passMilliseconds[LocalShadowPass];
        m_frameStats.GpuMainMilliseconds = passMilliseconds[MainHdrPass];
        m_frameStats.GpuPostMilliseconds = passMilliseconds[PostProcessPass];
        m_frameStats.GpuFrameMilliseconds = profile.FrameMilliseconds;

        if (m_timestampLogShadows[frameIndex])
        {
            if (const auto summary = m_shadowTiming.Submit(
                    passMilliseconds[DirectionalShadowPass]))
                log::Info(FormatGpuTiming("Vulkan directional shadows GPU", *summary));
            if (const auto summary = m_localShadowTiming.Submit(
                    passMilliseconds[LocalShadowPass]))
                log::Info(FormatGpuTiming("Vulkan local shadows GPU", *summary));
        }
        if (m_timestampLogPost[frameIndex])
        {
            if (const auto summary = m_mainTiming.Submit(passMilliseconds[MainHdrPass]))
                log::Info(FormatGpuTiming("Vulkan main HDR GPU", *summary));
            if (const auto summary = m_postTiming.Submit(passMilliseconds[PostProcessPass]))
            {
                const AntiAliasingMode mode = m_timestampAaMode[frameIndex];
                const char* modeName = mode == AntiAliasingMode::Taa ? "TAA"
                                     : mode == AntiAliasingMode::Fxaa ? "FXAA" : "NONE";
                log::Info(FormatGpuTiming(std::string("Vulkan post GPU (") + modeName + ")",
                                          *summary));
            }
        }
    }
    m_timestampFrameWritten[frameIndex] = false;
    m_pipelineStatisticsFrameWritten[frameIndex] = false;
}

} // namespace engine
