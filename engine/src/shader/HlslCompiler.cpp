#include "engine/shader/HlslCompiler.h"

#include "engine/render/ShaderSource.h"
#include "engine/render/ShaderPaths.h"

#include <shaderc/shaderc.hpp>

#include <stdexcept>
#include <string_view>

namespace engine::shader
{
namespace
{

shaderc_shader_kind ShaderKind(Stage stage)
{
    switch (stage)
    {
    case Stage::Vertex: return shaderc_vertex_shader;
    case Stage::Fragment: return shaderc_fragment_shader;
    case Stage::Compute: return shaderc_compute_shader;
    }
    throw std::runtime_error("Unsupported HLSL shader stage");
}

std::vector<std::filesystem::path> IncludeRoots(
    const std::filesystem::path& sourcePath)
{
    return {
        sourcePath.parent_path(),
        shader_paths::Root(),
        shader_paths::Resolve("hlsl"),
    };
}

void RemapHlslBuiltinsForOpenGL(std::vector<uint32_t>& spirv)
{
    // shaderc's HLSL frontend emits the Vulkan-oriented VertexIndex and
    // InstanceIndex builtins even for an OpenGL target environment. Native
    // GL_ARB_gl_spirv expects the OpenGL VertexId/InstanceId equivalents.
    constexpr uint16_t opDecorate = 71;
    constexpr uint32_t decorationBuiltIn = 11;
    constexpr uint32_t vertexId = 5;
    constexpr uint32_t instanceId = 6;
    constexpr uint32_t vertexIndex = 42;
    constexpr uint32_t instanceIndex = 43;

    for (size_t offset = 5; offset < spirv.size();)
    {
        const uint32_t instruction = spirv[offset];
        const uint16_t wordCount = static_cast<uint16_t>(instruction >> 16u);
        const uint16_t opcode = static_cast<uint16_t>(instruction & 0xffffu);
        if (wordCount == 0 || offset + wordCount > spirv.size())
            throw std::runtime_error("Malformed SPIR-V instruction stream");
        if (opcode == opDecorate && wordCount >= 4 &&
            spirv[offset + 2] == decorationBuiltIn)
        {
            uint32_t& builtin = spirv[offset + 3];
            if (builtin == vertexIndex)
                builtin = vertexId;
            else if (builtin == instanceIndex)
                builtin = instanceId;
        }
        offset += wordCount;
    }
}

} // namespace

const char* StageName(Stage stage) noexcept
{
    switch (stage)
    {
    case Stage::Vertex: return "vertex";
    case Stage::Fragment: return "fragment";
    case Stage::Compute: return "compute";
    }
    return "unknown";
}

const char* TargetName(SpirvTarget target) noexcept
{
    switch (target)
    {
    case SpirvTarget::OpenGL46: return "opengl-4.6-spirv-1.0-combined-samplers-gl-builtins-v2";
    case SpirvTarget::Vulkan13: return "vulkan-1.3-spirv-1.6";
    }
    return "unknown";
}

HlslCompileResult CompileHlslToSpirv(const HlslCompileRequest& request)
{
    if (request.SourcePath.empty())
        throw std::runtime_error("HLSL compile request has no source path");
    if (request.EntryPoint.empty())
        throw std::runtime_error("HLSL compile request has no entry point");

    HlslCompileResult output;
    const ShaderSourceDocument document = LoadShaderSource(
        request.SourcePath, IncludeRoots(request.SourcePath));
    output.Dependencies = document.Dependencies;
    output.ExpandedSource = ApplyShaderDefines(
        document.Source, CanonicalizeShaderDefines(request.Defines));
    output.ExpandedSource.insert(0, request.Target == SpirvTarget::Vulkan13
        ? "#define ENGINE_VULKAN 1\n"
        : "#define ENGINE_OPENGL 1\n");
    output.TargetIdentity = std::string(TargetName(request.Target)) + "-" +
        StageName(request.ShaderStage) + "-hlsl";
    output.PermutationKey = BuildShaderPermutationKey(
        document.Source, request.Defines, output.TargetIdentity);

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetSourceLanguage(shaderc_source_language_hlsl);
    options.SetHlslIoMapping(true);
    options.SetHlslOffsets(true);
    options.SetAutoMapLocations(true);
    if (request.Target == SpirvTarget::OpenGL46)
    {
        options.SetTargetEnvironment(
            shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);
        options.SetTargetSpirv(shaderc_spirv_version_1_0);
        // OpenGL exposes textures and samplers as a single sampler uniform.
        // HLSL normally emits separate Texture*/SamplerState variables, which
        // are legal for Vulkan but not a safe executable interface for
        // GL_ARB_gl_spirv drivers. Fold each used pair into OpTypeSampledImage.
        options.SetAutoSampledTextures(true);
    }
    else
    {
        options.SetTargetEnvironment(
            shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetTargetSpirv(shaderc_spirv_version_1_6);
    }
    options.SetOptimizationLevel(request.Optimize
        ? shaderc_optimization_level_performance
        : shaderc_optimization_level_zero);
    if (request.GenerateDebugInfo)
        options.SetGenerateDebugInfo();

    const std::string sourceName = request.SourcePath.string();
    const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        output.ExpandedSource, ShaderKind(request.ShaderStage),
        sourceName.c_str(), request.EntryPoint.c_str(), options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        throw std::runtime_error("HLSL compile error [" + output.TargetIdentity +
            "] (" + sourceName + "):\n" + result.GetErrorMessage());
    }
    output.Spirv.assign(result.cbegin(), result.cend());
    if (output.Spirv.empty() || output.Spirv.front() != 0x07230203u)
        throw std::runtime_error("HLSL compiler returned invalid SPIR-V: " + sourceName);
    if (request.Target == SpirvTarget::OpenGL46)
        RemapHlslBuiltinsForOpenGL(output.Spirv);
    return output;
}

} // namespace engine::shader
