#pragma once

#include "engine/renderer/GraphicsDevice.h"
#include "engine/shader/HlslCompiler.h"

#include <filesystem>
#include <unordered_map>
#include <vector>

namespace rendering::detail {

struct CompiledShader {
  ShaderModuleDesc Desc;
  std::vector<uint32_t> Spirv;
  ShaderReflection Reflection;
  std::vector<std::filesystem::path> Dependencies;
  std::unordered_map<std::filesystem::path, std::filesystem::file_time_type>
      WriteTimes;
};

CompiledShader CompileShader(const ShaderModuleDesc &desc,
                             engine::shader::SpirvTarget target);
ShaderReflection ReflectSpirv(std::span<const uint32_t> words,
                              ShaderStage stage, std::string entryPoint,
                              std::string permutationKey);
bool ShaderFilesChanged(const CompiledShader &shader);

} // namespace rendering::detail
