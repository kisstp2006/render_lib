#include "engine/backend/vk/VulkanPipelineCache.h"

#include "engine/core/Log.h"

#include <chrono>
#include <cstring>

namespace engine::vulkan
{
namespace
{

constexpr size_t kHeaderSize = 32;

double Milliseconds(std::chrono::steady_clock::time_point begin)
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
}

uint32_t ReadU32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8u) |
           (static_cast<uint32_t>(bytes[2]) << 16u) |
           (static_cast<uint32_t>(bytes[3]) << 24u);
}

} // namespace

bool PipelineCacheStore::Compatible(
    const std::vector<uint8_t>& bytes,
    const VkPhysicalDeviceProperties& properties) const
{
    if (bytes.size() < kHeaderSize)
        return false;
    return ReadU32(bytes.data()) == kHeaderSize &&
           ReadU32(bytes.data() + 4) == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           ReadU32(bytes.data() + 8) == properties.vendorID &&
           ReadU32(bytes.data() + 12) == properties.deviceID &&
           std::memcmp(bytes.data() + 16, properties.pipelineCacheUUID,
                       VK_UUID_SIZE) == 0;
}

void PipelineCacheStore::Init(
    VkPhysicalDevice physicalDevice, VkDevice device,
    const std::filesystem::path& cacheFile, bool enabled, bool clear,
    bool creationFeedback,
    PipelineCacheStatistics& statistics)
{
    m_device = device;
    m_cacheFile = cacheFile;
    m_statistics = &statistics;
    m_enabled = enabled;
    m_creationFeedback = creationFeedback;
    statistics.Enabled = enabled;
    statistics.CachePath = cacheFile.parent_path().string();

    if (clear)
    {
        std::error_code ignored;
        std::filesystem::remove(cacheFile, ignored);
        std::filesystem::remove(cacheFile.string() + ".tmp", ignored);
    }
    if (!enabled)
        return;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    std::vector<uint8_t> bytes;
    if (ReadBinaryFile(cacheFile, bytes) && Compatible(bytes, properties))
    {
        statistics.PersistentCacheLoaded = true;
        statistics.CacheBytesLoaded += bytes.size();
    }
    else
        bytes.clear();

    VkPipelineCacheCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    createInfo.initialDataSize = bytes.size();
    createInfo.pInitialData = bytes.empty() ? nullptr : bytes.data();
    VkResult result = vkCreatePipelineCache(device, &createInfo, nullptr, &m_cache);
    if (result != VK_SUCCESS && !bytes.empty())
    {
        log::Warn("Vulkan pipeline cache rejected persisted data; rebuilding an empty cache");
        createInfo.initialDataSize = 0;
        createInfo.pInitialData = nullptr;
        statistics.PersistentCacheLoaded = false;
        statistics.CacheBytesLoaded = 0;
        result = vkCreatePipelineCache(device, &createInfo, nullptr, &m_cache);
    }
    if (result != VK_SUCCESS)
    {
        log::Warn("Vulkan pipeline cache creation failed; continuing without persistence");
        m_enabled = false;
        statistics.Enabled = false;
    }
}

VkResult PipelineCacheStore::CreateGraphics(
    uint32_t count, const VkGraphicsPipelineCreateInfo* createInfos,
    VkPipeline* pipelines)
{
    const auto begin = std::chrono::steady_clock::now();
    std::vector<VkGraphicsPipelineCreateInfo> measuredInfos;
    std::vector<VkPipelineCreationFeedback> feedback;
    std::vector<VkPipelineCreationFeedbackCreateInfo> feedbackInfos;
    const VkGraphicsPipelineCreateInfo* submittedInfos = createInfos;
    if (m_creationFeedback)
    {
        measuredInfos.assign(createInfos, createInfos + count);
        feedback.resize(count);
        feedbackInfos.resize(count);
        for (uint32_t index = 0; index < count; ++index)
        {
            feedbackInfos[index].sType =
                VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO;
            feedbackInfos[index].pNext = measuredInfos[index].pNext;
            feedbackInfos[index].pPipelineCreationFeedback = &feedback[index];
            measuredInfos[index].pNext = &feedbackInfos[index];
        }
        submittedInfos = measuredInfos.data();
    }
    const VkResult result = vkCreateGraphicsPipelines(
        m_device, m_enabled ? m_cache : VK_NULL_HANDLE,
        count, submittedInfos, nullptr, pipelines);
    if (m_statistics)
    {
        m_statistics->PipelineCreateCalls += count;
        m_statistics->PipelineCreateMilliseconds += Milliseconds(begin);
        for (const VkPipelineCreationFeedback& item : feedback)
        {
            if ((item.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT) == 0)
                continue;
            if ((item.flags &
                 VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT) != 0)
                ++m_statistics->NativePipelineCacheHits;
            else
                ++m_statistics->NativePipelineCacheMisses;
        }
    }
    return result;
}

VkResult PipelineCacheStore::CreateCompute(
    uint32_t count, const VkComputePipelineCreateInfo* createInfos,
    VkPipeline* pipelines)
{
    const auto begin = std::chrono::steady_clock::now();
    std::vector<VkComputePipelineCreateInfo> measuredInfos;
    std::vector<VkPipelineCreationFeedback> feedback;
    std::vector<VkPipelineCreationFeedbackCreateInfo> feedbackInfos;
    const VkComputePipelineCreateInfo* submittedInfos = createInfos;
    if (m_creationFeedback)
    {
        measuredInfos.assign(createInfos, createInfos + count);
        feedback.resize(count);
        feedbackInfos.resize(count);
        for (uint32_t index = 0; index < count; ++index)
        {
            feedbackInfos[index].sType =
                VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO;
            feedbackInfos[index].pNext = measuredInfos[index].pNext;
            feedbackInfos[index].pPipelineCreationFeedback = &feedback[index];
            measuredInfos[index].pNext = &feedbackInfos[index];
        }
        submittedInfos = measuredInfos.data();
    }
    const VkResult result = vkCreateComputePipelines(
        m_device, m_enabled ? m_cache : VK_NULL_HANDLE,
        count, submittedInfos, nullptr, pipelines);
    if (m_statistics)
    {
        m_statistics->PipelineCreateCalls += count;
        m_statistics->PipelineCreateMilliseconds += Milliseconds(begin);
        for (const VkPipelineCreationFeedback& item : feedback)
        {
            if ((item.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT) == 0)
                continue;
            if ((item.flags &
                 VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT) != 0)
                ++m_statistics->NativePipelineCacheHits;
            else
                ++m_statistics->NativePipelineCacheMisses;
        }
    }
    return result;
}

bool PipelineCacheStore::Flush()
{
    if (!m_enabled || m_cache == VK_NULL_HANDLE)
        return true;
    size_t size = 0;
    if (vkGetPipelineCacheData(m_device, m_cache, &size, nullptr) != VK_SUCCESS || size == 0)
        return false;
    std::vector<uint8_t> bytes(size);
    if (vkGetPipelineCacheData(m_device, m_cache, &size, bytes.data()) != VK_SUCCESS)
        return false;
    bytes.resize(size);
    std::string error;
    if (!WriteBinaryFileAtomically(m_cacheFile, bytes.data(), bytes.size(), &error))
    {
        log::Warn("Vulkan pipeline cache write failed: " + error);
        return false;
    }
    if (m_statistics)
        m_statistics->CacheBytesSaved += bytes.size();
    return true;
}

void PipelineCacheStore::Shutdown()
{
    Flush();
    if (m_cache != VK_NULL_HANDLE)
        vkDestroyPipelineCache(m_device, m_cache, nullptr);
    m_cache = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_statistics = nullptr;
    m_enabled = false;
    m_creationFeedback = false;
}

} // namespace engine::vulkan
