#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

struct ShaderDefine
{
    std::string Name;
    std::string Value = "1";
};

struct PipelineCacheStatistics
{
    bool Enabled = false;
    bool PersistentCacheLoaded = false;
    uint64_t ShaderPermutationHits = 0;
    uint64_t ShaderPermutationMisses = 0;
    uint64_t NativePipelineCacheHits = 0;
    uint64_t NativePipelineCacheMisses = 0;
    uint64_t PipelineCreateCalls = 0;
    uint64_t CacheBytesLoaded = 0;
    uint64_t CacheBytesSaved = 0;
    double ShaderCompileMilliseconds = 0.0;
    double ShaderCacheLoadMilliseconds = 0.0;
    double PipelineCreateMilliseconds = 0.0;
    std::string CachePath;
};

struct PipelineStutterReport
{
    std::string Backend;
    std::string Adapter;
    bool ColdCacheRun = false;
    double ApplicationStartupMilliseconds = 0.0;
    PipelineCacheStatistics Cache;
    std::vector<double> FrameCpuMilliseconds;
};

std::vector<ShaderDefine> CanonicalizeShaderDefines(
    const std::vector<ShaderDefine>& defines);
std::string ApplyShaderDefines(std::string_view source,
                               const std::vector<ShaderDefine>& defines);
uint64_t StableShaderHash(std::string_view value,
                          uint64_t seed = 1469598103934665603ull);
std::string ShaderHashHex(uint64_t hash);
std::string BuildShaderPermutationKey(
    std::string_view expandedSource,
    const std::vector<ShaderDefine>& defines,
    std::string_view compilerTarget);

std::filesystem::path RendererCacheDirectory(
    const std::filesystem::path& root,
    std::string_view api,
    std::string_view deviceIdentity);
bool ClearRendererCacheFiles(const std::filesystem::path& directory,
                             std::string* error = nullptr);
bool ReadBinaryFile(const std::filesystem::path& path,
                    std::vector<uint8_t>& bytes);
bool WriteBinaryFileAtomically(const std::filesystem::path& path,
                               const void* data, size_t byteCount,
                               std::string* error = nullptr);
bool WritePipelineStutterReport(const std::filesystem::path& path,
                                const PipelineStutterReport& report,
                                std::string* error = nullptr);

} // namespace engine
