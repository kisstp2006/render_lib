#include "engine/backend/gl/GLShader.h"
#include "engine/backend/gl/GLDebug.h"
#include "engine/core/Log.h"
#include "engine/render/ShaderSource.h"
#include "engine/render/ShaderPaths.h"
#include "engine/shader/HlslCompiler.h"

#include <glad/gl.h>

#include <filesystem>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace engine {
namespace {

constexpr uint32_t kProgramBinaryMagic = 0x42474c53u; // SLGB
constexpr uint32_t kProgramBinaryVersion = 1;
// NVIDIA's OpenGL SPIR-V specialization can silently miscompile optimized
// shaderc HLSL without non-semantic debug records (observed as an all-black
// Release frame). Keep the records in every configuration; they do not alter
// shader semantics and are stripped by the driver's native program binary.
constexpr bool kGenerateOpenGlSpirvDebugInfo = true;

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

unsigned int GLShader::CompileStage(unsigned int stage,
                                    const std::vector<uint32_t>& spirv,
                                    const std::string& debugName)
{
    unsigned int handle = glCreateShader(stage);
    glShaderBinary(1, &handle, GL_SHADER_BINARY_FORMAT_SPIR_V,
                   spirv.data(), static_cast<GLsizei>(spirv.size() * sizeof(uint32_t)));
    glSpecializeShader(handle, "main", 0, nullptr, nullptr);

    int success = 0;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        int len = 0;
        glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 0 ? len : 1);
        glGetShaderInfoLog(handle, len, nullptr, log.data());
        glDeleteShader(handle);
        throw std::runtime_error("OpenGL SPIR-V specialization error (" +
                                 debugName + "): " + log.data());
    }

    return handle;
}

GLShader::GLShader(const std::string& vertPath, const std::string& fragPath,
                   const std::vector<ShaderDefine>& defines)
{
    const std::vector<std::filesystem::path> includeRoots = {
        shader_paths::Root(),
        shader_paths::Resolve("hlsl"),
        shader_paths::Resolve("hlsl/opengl")
    };
    const ShaderSourceDocument vertexDocument = LoadShaderSource(vertPath, includeRoots);
    const ShaderSourceDocument fragmentDocument = LoadShaderSource(fragPath, includeRoots);
    const std::vector<ShaderDefine> canonicalDefines = CanonicalizeShaderDefines(defines);

    GLShaderCacheState& cache = ShaderCacheState();
    const std::string vertexKey = BuildShaderPermutationKey(
        vertexDocument.Source, canonicalDefines, "opengl-4.6-native-spirv-hlsl-combined-gl-builtins-vertex-v4");
    const std::string fragmentKey = BuildShaderPermutationKey(
        fragmentDocument.Source, canonicalDefines, "opengl-4.6-native-spirv-hlsl-combined-gl-builtins-fragment-v4");
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
        shader::HlslCompileRequest vertexRequest;
        vertexRequest.SourcePath = vertPath;
        vertexRequest.ShaderStage = shader::Stage::Vertex;
        vertexRequest.Target = shader::SpirvTarget::OpenGL46;
        vertexRequest.Defines = canonicalDefines;
        vertexRequest.Optimize = true;
        vertexRequest.GenerateDebugInfo = kGenerateOpenGlSpirvDebugInfo;
        const shader::HlslCompileResult vertex =
            shader::CompileHlslToSpirv(vertexRequest);
        const unsigned int vert = CompileStage(
            GL_VERTEX_SHADER, vertex.Spirv, vertPath);
        unsigned int frag = 0;
        try
        {
            shader::HlslCompileRequest fragmentRequest;
            fragmentRequest.SourcePath = fragPath;
            fragmentRequest.ShaderStage = shader::Stage::Fragment;
            fragmentRequest.Target = shader::SpirvTarget::OpenGL46;
            fragmentRequest.Defines = canonicalDefines;
            fragmentRequest.Optimize = true;
            fragmentRequest.GenerateDebugInfo = kGenerateOpenGlSpirvDebugInfo;
            const shader::HlslCompileResult fragment =
                shader::CompileHlslToSpirv(fragmentRequest);
            frag = CompileStage(GL_FRAGMENT_SHADER, fragment.Spirv, fragPath);
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
    ReflectUniformBlocks();

    log::Info(std::string(loadedBinary ? "Loaded cached" : "Compiled") +
              " native SPIR-V HLSL shader program: " + vertPath + " + " + fragPath);
}

GLShader::GLShader(const std::string& computePath,
                   const std::vector<ShaderDefine>& defines)
{
    const std::vector<std::filesystem::path> includeRoots = {
        shader_paths::Root(),
        shader_paths::Resolve("hlsl"),
        shader_paths::Resolve("hlsl/opengl")
    };
    const ShaderSourceDocument document = LoadShaderSource(computePath, includeRoots);
    const std::vector<ShaderDefine> canonicalDefines = CanonicalizeShaderDefines(defines);

    GLShaderCacheState& cache = ShaderCacheState();
    const std::string shaderKey = BuildShaderPermutationKey(
        document.Source, canonicalDefines, "opengl-4.6-native-spirv-hlsl-combined-gl-builtins-compute-v4");
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
        shader::HlslCompileRequest request;
        request.SourcePath = computePath;
        request.ShaderStage = shader::Stage::Compute;
        request.Target = shader::SpirvTarget::OpenGL46;
        request.Defines = canonicalDefines;
        request.Optimize = true;
        request.GenerateDebugInfo = kGenerateOpenGlSpirvDebugInfo;
        const shader::HlslCompileResult result = shader::CompileHlslToSpirv(request);
        const unsigned int compute = CompileStage(
            GL_COMPUTE_SHADER, result.Spirv, computePath);
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
    ReflectUniformBlocks();
    log::Info(std::string(loadedBinary ? "Loaded cached" : "Compiled") +
              " native SPIR-V HLSL compute shader program: " + computePath);
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
    for (UniformBlock& block : m_uniformBlocks)
        if (block.Buffer)
            glDeleteBuffers(1, &block.Buffer);
    if (m_program)
        glDeleteProgram(m_program);
}

void GLShader::Use() const
{
    glUseProgram(m_program);
    for (const UniformBlock& block : m_uniformBlocks)
        glBindBufferBase(GL_UNIFORM_BUFFER, block.Binding, block.Buffer);
}

void GLShader::ReflectUniformBlocks()
{
    GLint blockCount = 0;
    glGetProgramInterfaceiv(m_program, GL_UNIFORM_BLOCK,
                            GL_ACTIVE_RESOURCES, &blockCount);
    if (blockCount <= 0)
        return;

    m_uniformBlocks.resize(static_cast<size_t>(blockCount));
    std::vector<std::string> blockNames(static_cast<size_t>(blockCount));
    constexpr std::array<GLenum, 3> blockProperties{
        GL_BUFFER_DATA_SIZE, GL_BUFFER_BINDING, GL_NAME_LENGTH};
    for (GLint index = 0; index < blockCount; ++index)
    {
        std::array<GLint, 3> values{};
        glGetProgramResourceiv(m_program, GL_UNIFORM_BLOCK, index,
            static_cast<GLsizei>(blockProperties.size()), blockProperties.data(),
            static_cast<GLsizei>(values.size()), nullptr, values.data());
        if (values[0] <= 0 || values[1] < 0)
            continue;

        std::string blockName(static_cast<size_t>(std::max(values[2], 1)), '\0');
        GLsizei written = 0;
        glGetProgramResourceName(m_program, GL_UNIFORM_BLOCK, index,
            static_cast<GLsizei>(blockName.size()), &written, blockName.data());
        blockName.resize(static_cast<size_t>(std::max<GLsizei>(written, 0)));
        blockNames[static_cast<size_t>(index)] = blockName;
        UniformBlock& block = m_uniformBlocks[static_cast<size_t>(index)];
        block.Binding = static_cast<unsigned int>(values[1]);
        block.ByteSize = values[0];
        std::vector<std::byte> zeros(static_cast<size_t>(block.ByteSize));
        glCreateBuffers(1, &block.Buffer);
        glNamedBufferData(block.Buffer, block.ByteSize, zeros.data(), GL_STREAM_DRAW);
        glUniformBlockBinding(m_program, static_cast<GLuint>(index), block.Binding);
        glBindBufferBase(GL_UNIFORM_BUFFER, block.Binding, block.Buffer);
        gl_debug::LabelObject(GL_BUFFER, block.Buffer,
            "Shader Constants: " + blockName);
    }

    GLint uniformCount = 0;
    glGetProgramInterfaceiv(m_program, GL_UNIFORM,
                            GL_ACTIVE_RESOURCES, &uniformCount);
    constexpr std::array<GLenum, 8> uniformProperties{
        GL_BLOCK_INDEX, GL_OFFSET, GL_ARRAY_SIZE, GL_ARRAY_STRIDE,
        GL_MATRIX_STRIDE, GL_IS_ROW_MAJOR, GL_TYPE, GL_NAME_LENGTH};
    for (GLint index = 0; index < uniformCount; ++index)
    {
        std::array<GLint, 8> values{};
        glGetProgramResourceiv(m_program, GL_UNIFORM, index,
            static_cast<GLsizei>(uniformProperties.size()), uniformProperties.data(),
            static_cast<GLsizei>(values.size()), nullptr, values.data());
        if (values[0] < 0 || values[0] >= blockCount)
            continue;

        std::string name(static_cast<size_t>(std::max(values[7], 1)), '\0');
        GLsizei written = 0;
        glGetProgramResourceName(m_program, GL_UNIFORM, index,
            static_cast<GLsizei>(name.size()), &written, name.data());
        name.resize(static_cast<size_t>(std::max<GLsizei>(written, 0)));
        if (name.empty())
            continue;
        BufferedUniform uniform;
        uniform.Block = static_cast<size_t>(values[0]);
        uniform.Offset = values[1];
        uniform.ArraySize = std::max(values[2], 1);
        uniform.ArrayStride = values[3];
        uniform.MatrixStride = values[4];
        uniform.RowMajor = values[5] != 0;
        uniform.Type = static_cast<unsigned int>(values[6]);
        m_bufferedUniforms.emplace(name, uniform);

        const std::string& blockName = blockNames[uniform.Block];
        const std::string prefix = blockName + ".";
        if (!blockName.empty() && name.starts_with(prefix))
            m_bufferedUniforms.emplace(name.substr(prefix.size()), uniform);
    }
}

const GLShader::BufferedUniform* GLShader::FindBufferedUniform(
    const std::string& name, int& elementOffset) const
{
    elementOffset = 0;
    if (const auto found = m_bufferedUniforms.find(name);
        found != m_bufferedUniforms.end())
        return &found->second;

    const size_t open = name.find('[');
    const size_t close = open == std::string::npos
        ? std::string::npos : name.find(']', open + 1);
    if (open == std::string::npos || close == std::string::npos)
        return nullptr;
    const std::string indexText = name.substr(open + 1, close - open - 1);
    if (indexText.empty() || !std::all_of(indexText.begin(), indexText.end(),
            [](unsigned char c) { return c >= '0' && c <= '9'; }))
        return nullptr;
    const int element = std::stoi(indexText);
    std::string normalized = name;
    normalized.replace(open + 1, close - open - 1, "0");
    const auto found = m_bufferedUniforms.find(normalized);
    if (found == m_bufferedUniforms.end() || element < 0 ||
        element >= found->second.ArraySize || found->second.ArrayStride <= 0)
        return nullptr;
    elementOffset = element * found->second.ArrayStride;
    return &found->second;
}

bool GLShader::WriteBufferedScalar(const std::string& name,
                                   const void* value, size_t byteCount)
{
    int elementOffset = 0;
    const BufferedUniform* uniform = FindBufferedUniform(name, elementOffset);
    if (!uniform || uniform->Block >= m_uniformBlocks.size())
        return false;
    const UniformBlock& block = m_uniformBlocks[uniform->Block];
    const GLintptr offset = uniform->Offset + elementOffset;
    if (!block.Buffer || offset < 0 ||
        static_cast<size_t>(offset) + byteCount > static_cast<size_t>(block.ByteSize))
        return false;
    glNamedBufferSubData(block.Buffer, offset,
                         static_cast<GLsizeiptr>(byteCount), value);
    return true;
}

bool GLShader::WriteBufferedVector(const std::string& name,
                                   const float* value, size_t componentCount)
{
    return WriteBufferedScalar(name, value, componentCount * sizeof(float));
}

bool GLShader::WriteBufferedMatrix(const std::string& name,
                                   const float* value, size_t dimensions)
{
    int elementOffset = 0;
    const BufferedUniform* uniform = FindBufferedUniform(name, elementOffset);
    if (!uniform || uniform->Block >= m_uniformBlocks.size() ||
        dimensions < 2 || dimensions > 4)
        return false;
    const UniformBlock& block = m_uniformBlocks[uniform->Block];
    const size_t stride = uniform->MatrixStride > 0
        ? static_cast<size_t>(uniform->MatrixStride)
        : dimensions * sizeof(float);
    std::array<std::byte, 64> packed{};
    for (size_t major = 0; major < dimensions; ++major)
    {
        float* destination = reinterpret_cast<float*>(packed.data() + major * stride);
        for (size_t minor = 0; minor < dimensions; ++minor)
        {
            destination[minor] = uniform->RowMajor
                ? value[minor * dimensions + major]
                : value[major * dimensions + minor];
        }
    }
    const GLintptr offset = uniform->Offset + elementOffset;
    const size_t byteCount = (dimensions - 1) * stride +
                             dimensions * sizeof(float);
    if (!block.Buffer || offset < 0 || byteCount > packed.size() ||
        static_cast<size_t>(offset) + byteCount > static_cast<size_t>(block.ByteSize))
        return false;
    glNamedBufferSubData(block.Buffer, offset,
                         static_cast<GLsizeiptr>(byteCount), packed.data());
    return true;
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
    const int integer = value ? 1 : 0;
    const int location = GetUniformLocation(name);
    if (location >= 0)
        glUniform1i(location, integer);
    else
        WriteBufferedScalar(name, &integer, sizeof(integer));
}

void GLShader::SetInt(const std::string& name, int value)
{
    const int location = GetUniformLocation(name);
    if (location >= 0)
        glUniform1i(location, value);
    else
        WriteBufferedScalar(name, &value, sizeof(value));
}

void GLShader::SetVec2(const std::string& name, const glm::vec2& value)
{
    const int location = GetUniformLocation(name);
    if (location >= 0)
        glUniform2fv(location, 1, &value[0]);
    else
        WriteBufferedVector(name, &value[0], 2);
}

void GLShader::SetVec4(const std::string& name, const glm::vec4& value)
{
    const int location = GetUniformLocation(name);
    if (location >= 0)
        glUniform4fv(location, 1, &value[0]);
    else
        WriteBufferedVector(name, &value[0], 4);
}

void GLShader::SetMat3(const std::string& name, const glm::mat3& value)
{
    const int location = GetUniformLocation(name);
    if (location >= 0)
        glUniformMatrix3fv(location, 1, GL_FALSE, &value[0][0]);
    else
        WriteBufferedMatrix(name, &value[0][0], 3);
}

void GLShader::SetFloat(const std::string& name, float value)
{
    const int location = GetUniformLocation(name);
    if (location >= 0)
        glUniform1f(location, value);
    else
        WriteBufferedScalar(name, &value, sizeof(value));
}

void GLShader::SetVec3(const std::string& name, const glm::vec3& value)
{
    const int location = GetUniformLocation(name);
    if (location >= 0)
        glUniform3fv(location, 1, &value[0]);
    else
        WriteBufferedVector(name, &value[0], 3);
}

void GLShader::SetMat4(const std::string& name, const glm::mat4& value)
{
    const int location = GetUniformLocation(name);
    if (location >= 0)
        glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
    else
        WriteBufferedMatrix(name, &value[0][0], 4);
}

} // namespace engine
