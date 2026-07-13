#include "engine/backend/vk/VulkanShaderCompiler.h"

#include "engine/backend/vk/VulkanResources.h"
#include "engine/core/Log.h"
#include "engine/render/ShaderSource.h"

#include <shaderc/shaderc.hpp>

#include <fstream>
#include <stdexcept>

namespace engine::vulkan {
namespace {

shaderc_shader_kind ShaderKind(const std::filesystem::path& path)
{
    const std::string extension = path.extension().string();
    if (extension == ".vert")
        return shaderc_vertex_shader;
    if (extension == ".frag")
        return shaderc_fragment_shader;
    if (extension == ".comp")
        return shaderc_compute_shader;
    throw std::runtime_error("Vulkan: unsupported runtime shader stage: " + path.string());
}

bool CacheIsCurrent(const std::filesystem::path& cachePath,
                    const std::vector<std::filesystem::path>& dependencies)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(cachePath, error))
        return false;

    const uintmax_t byteCount = std::filesystem::file_size(cachePath, error);
    if (error || byteCount == 0 || byteCount % sizeof(uint32_t) != 0)
        return false;
    std::ifstream input(cachePath, std::ios::binary);
    uint32_t magic = 0;
    if (!input.read(reinterpret_cast<char*>(&magic), sizeof(magic))
        || magic != 0x07230203u)
    {
        return false;
    }

    const auto cacheTime = std::filesystem::last_write_time(cachePath, error);
    if (error)
        return false;
    for (const auto& dependency : dependencies)
    {
        const auto sourceTime = std::filesystem::last_write_time(dependency, error);
        if (error || sourceTime > cacheTime)
            return false;
    }
    return true;
}

void WriteSpirv(const std::filesystem::path& cachePath,
                const shaderc::SpvCompilationResult& result)
{
    std::error_code error;
    std::filesystem::create_directories(cachePath.parent_path(), error);
    if (error)
        throw std::runtime_error("Vulkan: cannot create runtime shader cache directory: "
                                 + cachePath.parent_path().string());

    std::ofstream output(cachePath, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Vulkan: cannot write runtime shader cache: "
                                 + cachePath.string());
    for (const uint32_t word : result)
        output.write(reinterpret_cast<const char*>(&word), sizeof(word));
    if (!output)
        throw std::runtime_error("Vulkan: failed while writing runtime shader cache: "
                                 + cachePath.string());
}

} // namespace

VkShaderModule CompileAndLoadShaderModule(
    VkDevice device,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& cachePath,
    const std::vector<std::filesystem::path>& includeRoots)
{
    const ShaderSourceDocument source = LoadShaderSource(sourcePath, includeRoots);
    if (!CacheIsCurrent(cachePath, source.Dependencies))
    {
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetSourceLanguage(shaderc_source_language_glsl);
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetTargetSpirv(shaderc_spirv_version_1_6);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
#ifndef NDEBUG
        options.SetGenerateDebugInfo();
#endif

        const std::string sourceName = sourcePath.string();
        const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
            source.Source, ShaderKind(sourcePath), sourceName.c_str(), "main", options);
        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            throw std::runtime_error("Vulkan runtime shader compile error ("
                                     + sourceName + "):\n" + result.GetErrorMessage());
        }

        WriteSpirv(cachePath, result);
        log::Info("Runtime-compiled Vulkan shader: " + sourceName);
    }

    return LoadShaderModule(device, cachePath);
}

} // namespace engine::vulkan
