#pragma once

#include <filesystem>
#include <vector>

#include <vulkan/vulkan.h>

namespace engine::vulkan {

VkShaderModule CompileAndLoadShaderModule(
    VkDevice device,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& cachePath,
    const std::vector<std::filesystem::path>& includeRoots);

} // namespace engine::vulkan
