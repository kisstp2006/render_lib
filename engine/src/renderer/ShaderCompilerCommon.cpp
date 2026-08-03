#include "ShaderCompilerCommon.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace rendering::detail {
namespace {

engine::shader::Stage ToEngineStage(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return engine::shader::Stage::Vertex;
  case ShaderStage::Fragment:
    return engine::shader::Stage::Fragment;
  case ShaderStage::Compute:
    return engine::shader::Stage::Compute;
  }
  throw std::runtime_error("Unknown shader stage");
}

std::string SpirvString(std::span<const uint32_t> words, size_t firstWord) {
  if (firstWord >= words.size())
    return {};
  const char *value = reinterpret_cast<const char *>(words.data() + firstWord);
  const size_t available = (words.size() - firstWord) * sizeof(uint32_t);
  const void *end = std::memchr(value, 0, available);
  return end ? std::string(value, static_cast<const char *>(end))
             : std::string{};
}

struct Decoration {
  uint32_t Set = 0;
  uint32_t Binding = 0;
  uint32_t Location = 0;
  bool HasSet = false;
  bool HasBinding = false;
  bool HasLocation = false;
  bool Block = false;
  bool BufferBlock = false;
};

struct TypeInfo {
  uint16_t Opcode = 0;
  uint32_t StorageClass = 0;
  uint32_t ElementType = 0;
  uint32_t ArrayLengthId = 0;
  uint32_t ImageSampled = 1;
};

ShaderResourceType
Classify(uint32_t storageClass, uint32_t typeId,
         const std::unordered_map<uint32_t, TypeInfo> &types,
         const std::unordered_map<uint32_t, Decoration> &decorations) {
  constexpr uint32_t uniformConstant = 0;
  constexpr uint32_t input = 1;
  constexpr uint32_t uniform = 2;
  constexpr uint32_t pushConstant = 9;
  constexpr uint32_t storageBuffer = 12;
  if (storageClass == input)
    return ShaderResourceType::VertexInput;
  if (storageClass == pushConstant)
    return ShaderResourceType::PushConstants;
  if (storageClass == storageBuffer)
    return ShaderResourceType::StorageBuffer;

  auto type = types.find(typeId);
  while (type != types.end() &&
         (type->second.Opcode == 32 || type->second.Opcode == 28 ||
          type->second.Opcode == 29)) {
    typeId = type->second.ElementType;
    type = types.find(typeId);
  }
  if (storageClass == uniform) {
    const auto decoration = decorations.find(typeId);
    if (decoration != decorations.end() && decoration->second.BufferBlock)
      return ShaderResourceType::StorageBuffer;
    return ShaderResourceType::UniformBuffer;
  }
  if (storageClass == uniformConstant && type != types.end()) {
    if (type->second.Opcode == 26)
      return ShaderResourceType::Sampler;
    if (type->second.Opcode == 25)
      return type->second.ImageSampled == 2
                 ? ShaderResourceType::StorageTexture
                 : ShaderResourceType::SampledTexture;
    if (type->second.Opcode == 27)
      return ShaderResourceType::CombinedTextureSampler;
  }
  return ShaderResourceType::Unknown;
}

} // namespace

ShaderReflection ReflectSpirv(std::span<const uint32_t> words,
                              ShaderStage stage, std::string entryPoint,
                              std::string permutationKey) {
  if (words.size() < 5 || words[0] != 0x07230203u)
    throw std::runtime_error("Cannot reflect invalid SPIR-V");
  std::unordered_map<uint32_t, std::string> names;
  std::unordered_map<uint32_t, Decoration> decorations;
  std::unordered_map<uint32_t, TypeInfo> types;
  std::unordered_map<uint32_t, uint32_t> constants;
  struct Variable {
    uint32_t Type = 0;
    uint32_t Storage = 0;
  };
  std::unordered_map<uint32_t, Variable> variables;

  for (size_t offset = 5; offset < words.size();) {
    const uint32_t instruction = words[offset];
    const uint16_t count = static_cast<uint16_t>(instruction >> 16u);
    const uint16_t opcode = static_cast<uint16_t>(instruction & 0xffffu);
    if (count == 0 || offset + count > words.size())
      throw std::runtime_error("Malformed SPIR-V during reflection");
    const auto operand = [&](uint32_t index) { return words[offset + index]; };
    if (opcode == 5 && count >= 3) // OpName
      names[operand(1)] = SpirvString(words, offset + 2);
    else if (opcode == 71 && count >= 3) // OpDecorate
    {
      Decoration &d = decorations[operand(1)];
      const uint32_t kind = operand(2);
      if (kind == 2)
        d.Block = true;
      else if (kind == 3)
        d.BufferBlock = true;
      else if (kind == 30 && count >= 4) {
        d.Location = operand(3);
        d.HasLocation = true;
      } else if (kind == 33 && count >= 4) {
        d.Binding = operand(3);
        d.HasBinding = true;
      } else if (kind == 34 && count >= 4) {
        d.Set = operand(3);
        d.HasSet = true;
      }
    } else if (opcode == 25 && count >= 9) // OpTypeImage
      types[operand(1)] = {opcode, 0, 0, 0, operand(7)};
    else if ((opcode == 26 || opcode == 27 || opcode == 30) && count >= 2)
      types[operand(1)] = {opcode, 0, count >= 3 ? operand(2) : 0};
    else if (opcode == 28 && count >= 4) // OpTypeArray
      types[operand(1)] = {opcode, 0, operand(2), operand(3)};
    else if (opcode == 29 && count >= 3) // OpTypeRuntimeArray
      types[operand(1)] = {opcode, 0, operand(2)};
    else if (opcode == 32 && count >= 4) // OpTypePointer
      types[operand(1)] = {opcode, operand(2), operand(3)};
    else if (opcode == 43 && count >= 4) // OpConstant
      constants[operand(2)] = operand(3);
    else if (opcode == 59 && count >= 4) // OpVariable
      variables[operand(2)] = {operand(1), operand(3)};
    offset += count;
  }

  ShaderReflection reflection;
  reflection.Stage = stage;
  reflection.EntryPoint = std::move(entryPoint);
  reflection.PermutationKey = std::move(permutationKey);
  for (const auto &[id, variable] : variables) {
    const Decoration d = decorations[id];
    uint32_t pointedType = variable.Type;
    auto pointer = types.find(pointedType);
    if (pointer != types.end() && pointer->second.Opcode == 32)
      pointedType = pointer->second.ElementType;
    uint32_t arrayCount = 1;
    auto type = types.find(pointedType);
    if (type != types.end() && type->second.Opcode == 28) {
      if (const auto value = constants.find(type->second.ArrayLengthId);
          value != constants.end())
        arrayCount = value->second;
    }
    const ShaderResourceType resourceType =
        Classify(variable.Storage, variable.Type, types, decorations);
    if (resourceType == ShaderResourceType::Unknown)
      continue;
    ShaderResource resource;
    resource.Name =
        names.contains(id) ? names[id] : ("resource_" + std::to_string(id));
    resource.Type = resourceType;
    resource.Set = d.Set;
    resource.Binding = d.Binding;
    resource.Location = d.Location;
    resource.ArrayCount = arrayCount;
    reflection.Resources.push_back(std::move(resource));
  }
  return reflection;
}

CompiledShader CompileShader(const ShaderModuleDesc &desc,
                             engine::shader::SpirvTarget target) {
  CompiledShader output;
  output.Desc = desc;
  if (desc.Language == ShaderLanguage::SpirV) {
    std::vector<std::byte> bytes;
    if (!desc.Source.empty()) {
      bytes.resize(desc.Source.size());
      std::memcpy(bytes.data(), desc.Source.data(), desc.Source.size());
    } else {
      std::ifstream file(desc.SourcePath, std::ios::binary | std::ios::ate);
      if (!file)
        throw std::runtime_error("Cannot open SPIR-V: " +
                                 desc.SourcePath.string());
      const auto size = file.tellg();
      bytes.resize(static_cast<size_t>(size));
      file.seekg(0);
      file.read(reinterpret_cast<char *>(bytes.data()), size);
      output.Dependencies.push_back(desc.SourcePath);
    }
    if (bytes.size() % 4 != 0)
      throw std::runtime_error("SPIR-V byte size is not aligned");
    output.Spirv.resize(bytes.size() / 4);
    std::memcpy(output.Spirv.data(), bytes.data(), bytes.size());
    output.Reflection =
        ReflectSpirv(output.Spirv, desc.Stage, desc.EntryPoint, "precompiled");
  } else {
    engine::shader::ShaderCompileRequest request;
    request.SourcePath = desc.SourcePath;
    request.Source = desc.Source;
    request.Language = desc.Language == ShaderLanguage::Hlsl
                           ? engine::shader::SourceLanguage::Hlsl
                           : engine::shader::SourceLanguage::Glsl;
    request.ShaderStage = ToEngineStage(desc.Stage);
    request.Target = target;
    request.EntryPoint = desc.EntryPoint;
    request.Optimize = desc.Optimize;
    request.GenerateDebugInfo = desc.GenerateDebugInfo;
    request.Defines.reserve(desc.Defines.size());
    for (const ShaderDefine &define : desc.Defines)
      request.Defines.push_back({define.Name, define.Value});
    engine::shader::ShaderCompileResult compiled =
        engine::shader::CompileShaderToSpirv(request);
    output.Spirv = std::move(compiled.Spirv);
    output.Dependencies = std::move(compiled.Dependencies);
    output.Reflection = ReflectSpirv(output.Spirv, desc.Stage, desc.EntryPoint,
                                     std::move(compiled.PermutationKey));
  }
  for (const auto &dependency : output.Dependencies) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(dependency, error);
    if (!error)
      output.WriteTimes[dependency] = time;
  }
  return output;
}

bool ShaderFilesChanged(const CompiledShader &shader) {
  if (!shader.Desc.EnableHotReload || shader.Desc.SourcePath.empty())
    return false;
  for (const auto &[path, oldTime] : shader.WriteTimes) {
    std::error_code error;
    const auto current = std::filesystem::last_write_time(path, error);
    if (!error && current != oldTime)
      return true;
  }
  return false;
}

} // namespace rendering::detail
