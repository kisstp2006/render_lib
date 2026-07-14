#include "engine/backend/gl/GLShader.h"
#include "engine/backend/gl/GLDebug.h"
#include "engine/core/Log.h"
#include "engine/render/ShaderSource.h"

#include <glad/gl.h>

#include <filesystem>
#include <chrono>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace engine {
namespace {

constexpr uint32_t kProgramBinaryMagic = 0x42474c53u; // SLGB
constexpr uint32_t kProgramBinaryVersion = 1;

struct ProgramBinaryHeader
{
    uint32_t Magic = kProgramBinaryMagic;
    uint32_t Version = kProgramBinaryVersion;
    uint64_t Key = 0;
    uint32_t Format = 0;
    uint32_t ByteCount = 0;
};

struct GLShaderCacheState
{
    std::mutex Mutex;
    std::filesystem::path Directory;
    std::string DeviceIdentity;
    PipelineCacheStatistics Statistics;
};

GLShaderCacheState& ShaderCacheState()
{
    static GLShaderCacheState state;
    return state;
}

double Milliseconds(std::chrono::steady_clock::time_point begin)
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
}

bool LoadProgramBinary(const std::filesystem::path& path, uint64_t expectedKey,
                       unsigned int& program, uint64_t& loadedBytes)
{
    std::vector<uint8_t> bytes;
    if (!ReadBinaryFile(path, bytes) || bytes.size() < sizeof(ProgramBinaryHeader))
        return false;
    ProgramBinaryHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.Magic != kProgramBinaryMagic || header.Version != kProgramBinaryVersion ||
        header.Key != expectedKey || header.ByteCount == 0 ||
        bytes.size() != sizeof(header) + header.ByteCount)
        return false;

    program = glCreateProgram();
    glProgramBinary(program, header.Format, bytes.data() + sizeof(header),
                    static_cast<GLsizei>(header.ByteCount));
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        glDeleteProgram(program);
        program = 0;
        return false;
    }
    loadedBytes = bytes.size();
    return true;
}

void SaveProgramBinary(const std::filesystem::path& path, uint64_t key,
                       unsigned int program, PipelineCacheStatistics& statistics)
{
    GLint binaryLength = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    if (binaryLength <= 0)
        return;
    ProgramBinaryHeader header;
    header.Key = key;
    header.ByteCount = static_cast<uint32_t>(binaryLength);
    std::vector<uint8_t> bytes(sizeof(header) + static_cast<size_t>(binaryLength));
    GLsizei written = 0;
    GLenum format = 0;
    glGetProgramBinary(program, binaryLength, &written, &format,
                       bytes.data() + sizeof(header));
    if (written <= 0)
        return;
    header.Format = format;
    header.ByteCount = static_cast<uint32_t>(written);
    std::memcpy(bytes.data(), &header, sizeof(header));
    bytes.resize(sizeof(header) + static_cast<size_t>(written));
    std::string error;
    if (WriteBinaryFileAtomically(path, bytes.data(), bytes.size(), &error))
        statistics.CacheBytesSaved += bytes.size();
    else
        log::Warn("OpenGL program cache write failed: " + error);
}

} // namespace

unsigned int GLShader::CompileStage(unsigned int stage, const std::string& source, const std::string& debugName)
{
    unsigned int handle = glCreateShader(stage);
    const char* src = source.c_str();
    glShaderSource(handle, 1, &src, nullptr);
    glCompileShader(handle);

    int success = 0;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        int len = 0;
        glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 0 ? len : 1);
        glGetShaderInfoLog(handle, len, nullptr, log.data());
        throw std::runtime_error("Shader compile error (" + debugName + "): " + log.data());
    }

    return handle;
}

GLShader::GLShader(const std::string& vertPath, const std::string& fragPath,
                   const std::vector<ShaderDefine>& defines)
{
    const std::vector<std::filesystem::path> includeRoots = {
        std::filesystem::path(ENGINE_SHADER_DIR),
        std::filesystem::path(ENGINE_SHADER_DIR) / "gl"
    };
    const ShaderSourceDocument vertexDocument = LoadShaderSource(vertPath, includeRoots);
    const ShaderSourceDocument fragmentDocument = LoadShaderSource(fragPath, includeRoots);
    const std::vector<ShaderDefine> canonicalDefines = CanonicalizeShaderDefines(defines);
    const std::string vertexSource = ApplyShaderDefines(vertexDocument.Source, canonicalDefines);
    const std::string fragmentSource = ApplyShaderDefines(fragmentDocument.Source, canonicalDefines);

    GLShaderCacheState& cache = ShaderCacheState();
    const std::string vertexKey = BuildShaderPermutationKey(
        vertexDocument.Source, canonicalDefines, "opengl-4.6-vertex");
    const std::string fragmentKey = BuildShaderPermutationKey(
        fragmentDocument.Source, canonicalDefines, "opengl-4.6-fragment");
    const uint64_t programKey = StableShaderHash(
        vertexKey + ":" + fragmentKey + ":" + cache.DeviceIdentity);
    const std::filesystem::path binaryPath = cache.Directory /
        (ShaderHashHex(programKey) + ".glbin");
    const auto pipelineBegin = std::chrono::steady_clock::now();
    bool loadedBinary = false;
    if (cache.Statistics.Enabled)
    {
        uint64_t loadedBytes = 0;
        loadedBinary = LoadProgramBinary(binaryPath, programKey, m_program, loadedBytes);
        if (loadedBinary)
        {
            cache.Statistics.PersistentCacheLoaded = true;
            ++cache.Statistics.ShaderPermutationHits;
            ++cache.Statistics.NativePipelineCacheHits;
            cache.Statistics.CacheBytesLoaded += loadedBytes;
            cache.Statistics.ShaderCacheLoadMilliseconds += Milliseconds(pipelineBegin);
        }
        else
            ++cache.Statistics.NativePipelineCacheMisses;
    }

    if (!loadedBinary)
    {
        ++cache.Statistics.ShaderPermutationMisses;
        const auto compileBegin = std::chrono::steady_clock::now();
        const unsigned int vert = CompileStage(GL_VERTEX_SHADER, vertexSource, vertPath);
        unsigned int frag = 0;
        try
        {
            frag = CompileStage(GL_FRAGMENT_SHADER, fragmentSource, fragPath);
        }
        catch (...)
        {
            glDeleteShader(vert);
            throw;
        }
        cache.Statistics.ShaderCompileMilliseconds += Milliseconds(compileBegin);

        m_program = glCreateProgram();
        if (cache.Statistics.Enabled)
            glProgramParameteri(m_program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        glAttachShader(m_program, vert);
        glAttachShader(m_program, frag);
        glLinkProgram(m_program);

        int success = 0;
        glGetProgramiv(m_program, GL_LINK_STATUS, &success);
        if (!success)
        {
            int len = 0;
            glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(len > 0 ? len : 1);
            glGetProgramInfoLog(m_program, len, nullptr, log.data());
            glDeleteShader(vert);
            glDeleteShader(frag);
            glDeleteProgram(m_program);
            m_program = 0;
            throw std::runtime_error("Shader link error (" + vertPath + " / " + fragPath + "): " + log.data());
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
        if (cache.Statistics.Enabled)
            SaveProgramBinary(binaryPath, programKey, m_program, cache.Statistics);
    }
    ++cache.Statistics.PipelineCreateCalls;
    cache.Statistics.PipelineCreateMilliseconds += Milliseconds(pipelineBegin);

    const std::string programName = "Shader Program: " +
        std::filesystem::path(vertPath).filename().string() + " + " +
        std::filesystem::path(fragPath).filename().string();
    gl_debug::LabelObject(GL_PROGRAM, m_program, programName);

    log::Info(std::string(loadedBinary ? "Loaded cached" : "Compiled") +
              " shader program: " + vertPath + " + " + fragPath);
}

GLShader::GLShader(const std::string& computePath,
                   const std::vector<ShaderDefine>& defines)
{
    const std::vector<std::filesystem::path> includeRoots = {
        std::filesystem::path(ENGINE_SHADER_DIR),
        std::filesystem::path(ENGINE_SHADER_DIR) / "gl"
    };
    const ShaderSourceDocument document = LoadShaderSource(computePath, includeRoots);
    const std::vector<ShaderDefine> canonicalDefines = CanonicalizeShaderDefines(defines);
    const std::string source = ApplyShaderDefines(document.Source, canonicalDefines);

    GLShaderCacheState& cache = ShaderCacheState();
    const std::string shaderKey = BuildShaderPermutationKey(
        document.Source, canonicalDefines, "opengl-4.6-compute");
    const uint64_t programKey = StableShaderHash(shaderKey + ":" + cache.DeviceIdentity);
    const std::filesystem::path binaryPath = cache.Directory /
        (ShaderHashHex(programKey) + ".glbin");
    const auto pipelineBegin = std::chrono::steady_clock::now();
    bool loadedBinary = false;
    if (cache.Statistics.Enabled)
    {
        uint64_t loadedBytes = 0;
        loadedBinary = LoadProgramBinary(binaryPath, programKey, m_program, loadedBytes);
        if (loadedBinary)
        {
            cache.Statistics.PersistentCacheLoaded = true;
            ++cache.Statistics.ShaderPermutationHits;
            ++cache.Statistics.NativePipelineCacheHits;
            cache.Statistics.CacheBytesLoaded += loadedBytes;
            cache.Statistics.ShaderCacheLoadMilliseconds += Milliseconds(pipelineBegin);
        }
        else
            ++cache.Statistics.NativePipelineCacheMisses;
    }

    if (!loadedBinary)
    {
        ++cache.Statistics.ShaderPermutationMisses;
        const auto compileBegin = std::chrono::steady_clock::now();
        const unsigned int compute = CompileStage(GL_COMPUTE_SHADER, source, computePath);
        cache.Statistics.ShaderCompileMilliseconds += Milliseconds(compileBegin);
        m_program = glCreateProgram();
        if (cache.Statistics.Enabled)
            glProgramParameteri(m_program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        glAttachShader(m_program, compute);
        glLinkProgram(m_program);

        int success = 0;
        glGetProgramiv(m_program, GL_LINK_STATUS, &success);
        if (!success)
        {
            int len = 0;
            glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(len > 0 ? len : 1);
            glGetProgramInfoLog(m_program, len, nullptr, log.data());
            glDeleteShader(compute);
            glDeleteProgram(m_program);
            m_program = 0;
            throw std::runtime_error("Compute shader link error (" + computePath + "): " + log.data());
        }
        glDeleteShader(compute);
        if (cache.Statistics.Enabled)
            SaveProgramBinary(binaryPath, programKey, m_program, cache.Statistics);
    }
    ++cache.Statistics.PipelineCreateCalls;
    cache.Statistics.PipelineCreateMilliseconds += Milliseconds(pipelineBegin);
    gl_debug::LabelObject(GL_PROGRAM, m_program,
        "Compute Shader Program: " + std::filesystem::path(computePath).filename().string());
    log::Info(std::string(loadedBinary ? "Loaded cached" : "Compiled") +
              " compute shader program: " + computePath);
}

void GLShader::ConfigureCache(const std::filesystem::path& root,
                              std::string deviceIdentity,
                              bool enabled, bool clear)
{
    GLShaderCacheState& cache = ShaderCacheState();
    std::scoped_lock lock(cache.Mutex);
    cache.DeviceIdentity = std::move(deviceIdentity);
    cache.Directory = RendererCacheDirectory(root, "opengl", cache.DeviceIdentity);
    cache.Statistics = {};
    cache.Statistics.Enabled = enabled;
    cache.Statistics.CachePath = cache.Directory.string();
    if (clear)
    {
        std::string error;
        if (!ClearRendererCacheFiles(cache.Directory, &error))
            log::Warn("OpenGL cache clear failed: " + error);
    }
}

PipelineCacheStatistics GLShader::CacheStatistics()
{
    GLShaderCacheState& cache = ShaderCacheState();
    std::scoped_lock lock(cache.Mutex);
    return cache.Statistics;
}

GLShader::~GLShader()
{
    if (m_program)
        glDeleteProgram(m_program);
}

void GLShader::Use() const
{
    glUseProgram(m_program);
}

int GLShader::GetUniformLocation(const std::string& name)
{
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end())
        return it->second;

    const int loc = glGetUniformLocation(m_program, name.c_str());
    m_uniformCache[name] = loc;
    return loc;
}

void GLShader::SetBool(const std::string& name, bool value)
{
    glUniform1i(GetUniformLocation(name), value ? 1 : 0);
}

void GLShader::SetInt(const std::string& name, int value)
{
    glUniform1i(GetUniformLocation(name), value);
}

void GLShader::SetVec2(const std::string& name, const glm::vec2& value)
{
    glUniform2fv(GetUniformLocation(name), 1, &value[0]);
}

void GLShader::SetVec4(const std::string& name, const glm::vec4& value)
{
    glUniform4fv(GetUniformLocation(name), 1, &value[0]);
}

void GLShader::SetMat3(const std::string& name, const glm::mat3& value)
{
    glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

void GLShader::SetFloat(const std::string& name, float value)
{
    glUniform1f(GetUniformLocation(name), value);
}

void GLShader::SetVec3(const std::string& name, const glm::vec3& value)
{
    glUniform3fv(GetUniformLocation(name), 1, &value[0]);
}

void GLShader::SetMat4(const std::string& name, const glm::mat4& value)
{
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

} // namespace engine
