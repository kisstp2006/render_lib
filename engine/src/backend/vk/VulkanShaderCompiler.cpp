#include "engine/backend/vk/VulkanShaderCompiler.h"

#include "engine/backend/vk/VulkanResources.h"
#include "engine/core/Log.h"
#include "engine/render/ShaderSource.h"
#include "engine/shader/HlslCompiler.h"

#include <fstream>
#include <chrono>
#include <stdexcept>

namespace engine::vulkan {
namespace {

shader::Stage ShaderStage(const std::filesystem::path& path)
{
    const std::string filename = path.filename().string();
    if (filename.ends_with(".vert.hlsl"))
        return shader::Stage::Vertex;
    if (filename.ends_with(".frag.hlsl"))
        return shader::Stage::Fragment;
    if (filename.ends_with(".comp.hlsl"))
        return shader::Stage::Compute;
    throw std::runtime_error("Vulkan: unsupported runtime shader stage: " + path.string());
}

bool CacheIsValid(const std::filesystem::path& cachePath)
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

    return true;
}

void WriteSpirv(const std::filesystem::path& cachePath,
                const std::vector<uint32_t>& spirv)
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
    for (const uint32_t word : spirv)
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
    const std::vector<std::filesystem::path>& includeRoots,
    const std::vector<ShaderDefine>& defines,
    bool cacheEnabled,
    PipelineCacheStatistics* statistics)
{
    const ShaderSourceDocument source = LoadShaderSource(sourcePath, includeRoots);
    const std::vector<ShaderDefine> canonicalDefines = CanonicalizeShaderDefines(defines);
#ifdef NDEBUG
    constexpr std::string_view buildMode = "release";
#else
    constexpr std::string_view buildMode = "debug";
#endif
    const shader::Stage stage = ShaderStage(sourcePath);
    const std::string target = "vulkan-1.3-spirv-1.6-performance-hlsl-" +
        std::string(buildMode) + "-" + shader::StageName(stage);
    const std::string permutationKey = BuildShaderPermutationKey(
        source.Source, canonicalDefines, target);
    const std::filesystem::path keyedCachePath = cachePath.parent_path() /
        (cachePath.stem().string() + "." + permutationKey + ".spv");
    const auto loadBegin = std::chrono::steady_clock::now();
    const bool cacheHit = cacheEnabled && CacheIsValid(keyedCachePath);
    if (statistics)
    {
        if (cacheHit)
        {
            ++statistics->ShaderPermutationHits;
            statistics->PersistentCacheLoaded = true;
            std::error_code sizeError;
            const uintmax_t size = std::filesystem::file_size(keyedCachePath, sizeError);
            if (!sizeError)
                statistics->CacheBytesLoaded += size;
            statistics->ShaderCacheLoadMilliseconds +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - loadBegin).count();
        }
        else
            ++statistics->ShaderPermutationMisses;
    }
    if (!cacheHit)
    {
        const auto compileBegin = std::chrono::steady_clock::now();
        shader::HlslCompileRequest request;
        request.SourcePath = sourcePath;
        request.ShaderStage = stage;
        request.Target = shader::SpirvTarget::Vulkan13;
        request.Defines = canonicalDefines;
        request.Optimize = true;
#ifndef NDEBUG
        request.GenerateDebugInfo = true;
#endif
        const shader::HlslCompileResult result = shader::CompileHlslToSpirv(request);
        WriteSpirv(keyedCachePath, result.Spirv);
        if (statistics)
        {
            statistics->ShaderCompileMilliseconds +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - compileBegin).count();
            std::error_code sizeError;
            const uintmax_t size = std::filesystem::file_size(keyedCachePath, sizeError);
            if (!sizeError)
                statistics->CacheBytesSaved += size;
        }
        log::Info("Runtime-compiled Vulkan HLSL shader: " + sourcePath.string());
    }

    VkShaderModule module = LoadShaderModule(device, keyedCachePath);
    if (!cacheEnabled)
    {
        std::error_code ignored;
        std::filesystem::remove(keyedCachePath, ignored);
    }
    return module;
}

} // namespace engine::vulkan
