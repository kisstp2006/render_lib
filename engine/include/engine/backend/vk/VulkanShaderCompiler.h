#pragma once

#include <filesystem>
#include <vector>

#include <vulkan/vulkan.h>
#include "engine/render/PipelineCache.h"

namespace engine::vulkan {

VkShaderModule CompileAndLoadShaderModule(
    VkDevice device,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& cachePath,
    const std::vector<std::filesystem::path>& includeRoots,
    const std::vector<ShaderDefine>& defines = {},
    bool cacheEnabled = true,
    PipelineCacheStatistics* statistics = nullptr);

} // namespace engine::vulkan
