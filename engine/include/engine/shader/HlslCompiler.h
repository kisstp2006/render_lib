#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/render/PipelineCache.h"

namespace engine::shader {

enum class Stage : uint8_t {
  Vertex,
  Fragment,
  Compute,
};

enum class SpirvTarget : uint8_t {
  OpenGL46,
  Vulkan13,
};

enum class SourceLanguage : uint8_t {
  Hlsl,
  Glsl,
};

struct ShaderCompileRequest {
  std::filesystem::path SourcePath;
  // Optional in-memory source. SourcePath remains the virtual source name
  // and include root when this is populated.
  std::string Source;
  SourceLanguage Language = SourceLanguage::Hlsl;
  Stage ShaderStage = Stage::Vertex;
  SpirvTarget Target = SpirvTarget::Vulkan13;
  std::string EntryPoint = "main";
  std::vector<ShaderDefine> Defines;
  bool Optimize = true;
  bool GenerateDebugInfo = false;
};

struct ShaderCompileResult {
  std::vector<uint32_t> Spirv;
  std::vector<std::filesystem::path> Dependencies;
  std::string ExpandedSource;
  std::string PermutationKey;
  std::string TargetIdentity;
};

using HlslCompileRequest = ShaderCompileRequest;
using HlslCompileResult = ShaderCompileResult;

// Compiles HLSL directly to client-compatible SPIR-V. OpenGL output follows
// the OpenGL SPIR-V environment and is intended for glShaderBinary plus
// glSpecializeShader; Vulkan output targets Vulkan 1.3 / SPIR-V 1.6.
HlslCompileResult CompileHlslToSpirv(const HlslCompileRequest &request);
ShaderCompileResult CompileShaderToSpirv(const ShaderCompileRequest &request);

const char *StageName(Stage stage) noexcept;
const char *TargetName(SpirvTarget target) noexcept;

} // namespace engine::shader
