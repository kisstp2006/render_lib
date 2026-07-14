#include "engine/render/PipelineCache.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace engine
{
namespace
{

bool ValidIdentifier(std::string_view name)
{
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) ||
                          name.front() == '_'))
        return false;
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char c)
    {
        return std::isalnum(c) || c == '_';
    });
}

std::string Json(std::string_view value)
{
    std::string result = "\"";
    for (const char c : value)
    {
        switch (c)
        {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += c; break;
        }
    }
    result += '"';
    return result;
}

double Percentile(std::vector<double> values, double percentile)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(std::ceil(
        percentile * static_cast<double>(values.size()))) - 1u;
    return values[std::min(index, values.size() - 1u)];
}

} // namespace

std::vector<ShaderDefine> CanonicalizeShaderDefines(
    const std::vector<ShaderDefine>& defines)
{
    std::vector<ShaderDefine> result = defines;
    for (ShaderDefine& define : result)
    {
        if (!ValidIdentifier(define.Name))
            throw std::runtime_error("Invalid shader permutation define: " + define.Name);
        if (define.Value.empty())
            define.Value = "1";
        if (define.Value.find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("Shader define value contains a newline: " + define.Name);
    }
    std::sort(result.begin(), result.end(), [](const ShaderDefine& left,
                                                const ShaderDefine& right)
    {
        return left.Name < right.Name;
    });
    for (size_t index = 1; index < result.size(); ++index)
    {
        if (result[index - 1].Name == result[index].Name)
        {
            if (result[index - 1].Value != result[index].Value)
                throw std::runtime_error("Conflicting shader define: " + result[index].Name);
            result.erase(result.begin() + static_cast<std::ptrdiff_t>(index));
            --index;
        }
    }
    return result;
}

std::string ApplyShaderDefines(std::string_view source,
                               const std::vector<ShaderDefine>& defines)
{
    const std::vector<ShaderDefine> canonical = CanonicalizeShaderDefines(defines);
    if (canonical.empty())
        return std::string(source);
    std::string block;
    for (const ShaderDefine& define : canonical)
        block += "#define " + define.Name + " " + define.Value + "\n";

    const size_t version = source.find("#version");
    if (version == std::string_view::npos)
        return block + std::string(source);
    const size_t lineEnd = source.find('\n', version);
    if (lineEnd == std::string_view::npos)
        return std::string(source) + "\n" + block;
    std::string result(source.substr(0, lineEnd + 1));
    result += block;
    result.append(source.substr(lineEnd + 1));
    return result;
}

uint64_t StableShaderHash(std::string_view value, uint64_t seed)
{
    uint64_t hash = seed;
    for (const unsigned char c : value)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string ShaderHashHex(uint64_t hash)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::string BuildShaderPermutationKey(
    std::string_view expandedSource, const std::vector<ShaderDefine>& defines,
    std::string_view compilerTarget)
{
    uint64_t hash = StableShaderHash("source-like-shader-cache-v1\n");
    hash = StableShaderHash(compilerTarget, hash);
    hash = StableShaderHash("\n", hash);
    for (const ShaderDefine& define : CanonicalizeShaderDefines(defines))
    {
        hash = StableShaderHash(define.Name, hash);
        hash = StableShaderHash("=", hash);
        hash = StableShaderHash(define.Value, hash);
        hash = StableShaderHash("\n", hash);
    }
    hash = StableShaderHash(expandedSource, hash);
    return ShaderHashHex(hash);
}

std::filesystem::path RendererCacheDirectory(
    const std::filesystem::path& root, std::string_view api,
    std::string_view deviceIdentity)
{
    const std::string deviceKey = ShaderHashHex(StableShaderHash(deviceIdentity));
    return root / "SourceLikeRenderer" / std::string(api) / deviceKey;
}

bool ClearRendererCacheFiles(const std::filesystem::path& directory,
                             std::string* error)
{
    std::error_code iteratorError;
    if (!std::filesystem::exists(directory, iteratorError))
        return true;
    for (std::filesystem::recursive_directory_iterator iterator(directory, iteratorError), end;
         !iteratorError && iterator != end; iterator.increment(iteratorError))
    {
        if (!iterator->is_regular_file())
            continue;
        const std::string extension = iterator->path().extension().string();
        if (extension != ".spv" && extension != ".glbin" &&
            extension != ".vkc" && extension != ".tmp")
            continue;
        std::error_code removeError;
        std::filesystem::remove(iterator->path(), removeError);
        if (removeError)
        {
            if (error) *error = "Cannot clear renderer cache file: " + removeError.message();
            return false;
        }
    }
    if (iteratorError)
    {
        if (error) *error = "Cannot enumerate renderer cache: " + iteratorError.message();
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ReadBinaryFile(const std::filesystem::path& path,
                    std::vector<uint8_t>& bytes)
{
    bytes.clear();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return false;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > static_cast<uintmax_t>(SIZE_MAX))
        return false;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    bytes.resize(static_cast<size_t>(size));
    return static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()),
                                       static_cast<std::streamsize>(bytes.size())));
}

bool WriteBinaryFileAtomically(const std::filesystem::path& path,
                               const void* data, size_t byteCount,
                               std::string* error)
{
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError)
    {
        if (error) *error = "Cannot create cache directory: " + directoryError.message();
        return false;
    }
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file || (byteCount > 0 &&
            !file.write(static_cast<const char*>(data),
                        static_cast<std::streamsize>(byteCount))))
        {
            if (error) *error = "Cannot write cache file: " + temporary.string();
            return false;
        }
    }
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    std::error_code renameError;
    std::filesystem::rename(temporary, path, renameError);
    if (renameError)
    {
        if (error) *error = "Cannot publish cache file: " + renameError.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    if (error) error->clear();
    return true;
}

bool WritePipelineStutterReport(const std::filesystem::path& path,
                                const PipelineStutterReport& report,
                                std::string* error)
{
    std::error_code directoryError;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), directoryError);
    std::ofstream file(path, std::ios::trunc);
    if (directoryError || !file)
    {
        if (error) *error = directoryError ? directoryError.message() :
            "Cannot open pipeline stutter report: " + path.string();
        return false;
    }
    double total = 0.0;
    double maximum = 0.0;
    for (const double frame : report.FrameCpuMilliseconds)
    {
        total += frame;
        maximum = std::max(maximum, frame);
    }
    const double average = report.FrameCpuMilliseconds.empty() ? 0.0 :
        total / static_cast<double>(report.FrameCpuMilliseconds.size());
    const PipelineCacheStatistics& cache = report.Cache;
    file << std::fixed << std::setprecision(4)
         << "{\n  \"schema\": 1,\n"
         << "  \"backend\": " << Json(report.Backend) << ",\n"
         << "  \"adapter\": " << Json(report.Adapter) << ",\n"
         << "  \"cold_cache\": " << (report.ColdCacheRun ? "true" : "false") << ",\n"
         << "  \"startup_ms\": " << report.ApplicationStartupMilliseconds << ",\n"
         << "  \"frame_count\": " << report.FrameCpuMilliseconds.size() << ",\n"
         << "  \"frame_average_ms\": " << average << ",\n"
         << "  \"frame_p95_ms\": " << Percentile(report.FrameCpuMilliseconds, 0.95) << ",\n"
         << "  \"frame_max_ms\": " << maximum << ",\n"
         << "  \"cache\": {\n"
         << "    \"enabled\": " << (cache.Enabled ? "true" : "false") << ",\n"
         << "    \"persistent_cache_loaded\": " << (cache.PersistentCacheLoaded ? "true" : "false") << ",\n"
         << "    \"shader_hits\": " << cache.ShaderPermutationHits << ",\n"
         << "    \"shader_misses\": " << cache.ShaderPermutationMisses << ",\n"
         << "    \"native_hits\": " << cache.NativePipelineCacheHits << ",\n"
         << "    \"native_misses\": " << cache.NativePipelineCacheMisses << ",\n"
         << "    \"pipeline_create_calls\": " << cache.PipelineCreateCalls << ",\n"
         << "    \"bytes_loaded\": " << cache.CacheBytesLoaded << ",\n"
         << "    \"bytes_saved\": " << cache.CacheBytesSaved << ",\n"
         << "    \"shader_compile_ms\": " << cache.ShaderCompileMilliseconds << ",\n"
         << "    \"shader_cache_load_ms\": " << cache.ShaderCacheLoadMilliseconds << ",\n"
         << "    \"pipeline_create_ms\": " << cache.PipelineCreateMilliseconds << ",\n"
         << "    \"path\": " << Json(cache.CachePath) << "\n  },\n"
         << "  \"frames_ms\": [";
    for (size_t index = 0; index < report.FrameCpuMilliseconds.size(); ++index)
        file << (index ? ", " : "") << report.FrameCpuMilliseconds[index];
    file << "]\n}\n";
    if (!file)
    {
        if (error) *error = "Failed while writing pipeline stutter report: " + path.string();
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace engine
