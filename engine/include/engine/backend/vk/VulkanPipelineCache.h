#pragma once

#include <filesystem>

#include <vulkan/vulkan.h>

#include "engine/render/PipelineCache.h"

namespace engine::vulkan
{

class PipelineCacheStore
{
public:
    void Init(VkPhysicalDevice physicalDevice, VkDevice device,
              const std::filesystem::path& cacheFile, bool enabled,
              bool clear, bool creationFeedback,
              PipelineCacheStatistics& statistics);
    void Shutdown();

    VkResult CreateGraphics(uint32_t count,
                            const VkGraphicsPipelineCreateInfo* createInfos,
                            VkPipeline* pipelines);
    VkResult CreateCompute(uint32_t count,
                           const VkComputePipelineCreateInfo* createInfos,
                           VkPipeline* pipelines);
    bool Flush();
    VkPipelineCache Handle() const { return m_cache; }

private:
    bool Compatible(const std::vector<uint8_t>& bytes,
                    const VkPhysicalDeviceProperties& properties) const;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineCache m_cache = VK_NULL_HANDLE;
    std::filesystem::path m_cacheFile;
    PipelineCacheStatistics* m_statistics = nullptr;
    bool m_enabled = false;
    bool m_creationFeedback = false;
};

} // namespace engine::vulkan
