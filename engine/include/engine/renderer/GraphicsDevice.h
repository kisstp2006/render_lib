#pragma once

#include "engine/renderer/Export.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rendering {

using ShaderModuleHandle = uint64_t;
using GraphicsPipelineHandle = uint64_t;
using ComputePipelineHandle = uint64_t;
using BufferHandle = uint64_t;
using TextureHandle = uint64_t;
using SamplerHandle = uint64_t;
using RenderTargetHandle = uint64_t;

enum class ShaderLanguage : uint32_t { Hlsl, Glsl, SpirV };
enum class ShaderStage : uint32_t { Vertex, Fragment, Compute };

struct ShaderDefine {
  std::string Name;
  std::string Value = "1";
};

enum class ShaderResourceType : uint32_t {
  Unknown,
  VertexInput,
  UniformBuffer,
  StorageBuffer,
  SampledTexture,
  CombinedTextureSampler,
  StorageTexture,
  Sampler,
  PushConstants,
};

struct ShaderResource {
  std::string Name;
  ShaderResourceType Type = ShaderResourceType::Unknown;
  uint32_t Set = 0;
  uint32_t Binding = 0;
  uint32_t Location = 0;
  uint32_t ArrayCount = 1;
};

struct ShaderReflection {
  ShaderStage Stage = ShaderStage::Vertex;
  std::string EntryPoint = "main";
  std::vector<ShaderResource> Resources;
  std::string PermutationKey;
  uint64_t Generation = 1;
};

struct ShaderReflectionView {
  ShaderStage Stage = ShaderStage::Vertex;
  std::string_view EntryPoint;
  std::span<const ShaderResource> Resources;
  std::string_view PermutationKey;
  uint64_t Generation = 0;
};

struct ShaderModuleDesc {
  ShaderStage Stage = ShaderStage::Vertex;
  ShaderLanguage Language = ShaderLanguage::Hlsl;
  std::filesystem::path SourcePath;
  // Source and SourcePath are mutually compatible: Source overrides file
  // contents while SourcePath remains the virtual filename/include root.
  std::string Source;
  std::string EntryPoint = "main";
  std::vector<ShaderDefine> Defines;
  bool Optimize = true;
  bool GenerateDebugInfo = false;
  bool EnableHotReload = true;
  std::string DebugName;
};

enum class VertexFormat : uint32_t {
  Float,
  Float2,
  Float3,
  Float4,
  UInt,
  UInt2,
  UInt4,
  UByte4Normalized,
};

enum class VertexInputRate : uint32_t { PerVertex, PerInstance };

struct VertexBindingDesc {
  uint32_t Binding = 0;
  uint32_t Stride = 0;
  VertexInputRate InputRate = VertexInputRate::PerVertex;
};

struct VertexAttributeDesc {
  uint32_t Location = 0;
  uint32_t Binding = 0;
  VertexFormat Format = VertexFormat::Float3;
  uint32_t Offset = 0;
};

enum class PrimitiveTopology : uint32_t {
  TriangleList,
  TriangleStrip,
  LineList,
  PointList
};
enum class CullMode : uint32_t { None, Front, Back };
enum class FrontFace : uint32_t { CounterClockwise, Clockwise };
enum class CompareOperation : uint32_t {
  Never,
  Less,
  LessEqual,
  Equal,
  GreaterEqual,
  Greater,
  Always
};
enum class TextureFormat : uint32_t {
  R8Unorm,
  RG8Unorm,
  RGBA8Unorm,
  RGBA8Srgb,
  RGBA16Float,
  R32Float,
  Depth32Float,
};

struct BlendState {
  bool Enabled = false;
};

struct GraphicsPipelineDesc {
  ShaderModuleHandle VertexShader = 0;
  ShaderModuleHandle FragmentShader = 0;
  std::vector<VertexBindingDesc> VertexBindings;
  std::vector<VertexAttributeDesc> VertexAttributes;
  PrimitiveTopology Topology = PrimitiveTopology::TriangleList;
  CullMode Cull = CullMode::Back;
  FrontFace Winding = FrontFace::CounterClockwise;
  bool DepthTest = false;
  bool DepthWrite = false;
  CompareOperation DepthCompare = CompareOperation::Less;
  BlendState Blend;
  // Empty color formats select the presentation target format.
  std::vector<TextureFormat> ColorFormats;
  // Depth is disabled when this is not set, independently of DepthTest.
  bool HasDepthFormat = false;
  TextureFormat DepthFormat = TextureFormat::Depth32Float;
  std::string DebugName;
};

struct ComputePipelineDesc {
  ShaderModuleHandle ComputeShader = 0;
  std::string DebugName;
};

enum class BufferUsage : uint32_t {
  Vertex = 1u << 0u,
  Index = 1u << 1u,
  Uniform = 1u << 2u,
  Storage = 1u << 3u,
  TransferSource = 1u << 4u,
  TransferDestination = 1u << 5u,
};

constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
  return static_cast<BufferUsage>(static_cast<uint32_t>(a) |
                                  static_cast<uint32_t>(b));
}

struct BufferDesc {
  uint64_t Size = 0;
  BufferUsage Usage = BufferUsage::Vertex;
  bool CpuWritable = false;
  std::string DebugName;
};

enum class TextureUsage : uint32_t {
  Sampled = 1u << 0u,
  Storage = 1u << 1u,
  ColorAttachment = 1u << 2u,
  DepthAttachment = 1u << 3u,
  TransferSource = 1u << 4u,
  TransferDestination = 1u << 5u,
};

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept {
  return static_cast<TextureUsage>(static_cast<uint32_t>(a) |
                                   static_cast<uint32_t>(b));
}

struct TextureDesc {
  uint32_t Width = 1;
  uint32_t Height = 1;
  uint32_t MipLevels = 1;
  TextureFormat Format = TextureFormat::RGBA8Unorm;
  TextureUsage Usage = TextureUsage::Sampled;
  std::string DebugName;
};

enum class Filter : uint32_t { Nearest, Linear };
enum class AddressMode : uint32_t {
  Repeat,
  MirroredRepeat,
  ClampToEdge,
  ClampToBorder
};

struct SamplerDesc {
  Filter MinFilter = Filter::Linear;
  Filter MagFilter = Filter::Linear;
  AddressMode AddressU = AddressMode::Repeat;
  AddressMode AddressV = AddressMode::Repeat;
  AddressMode AddressW = AddressMode::Repeat;
  float MaxAnisotropy = 1.0f;
  std::string DebugName;
};

struct RenderTargetDesc {
  std::vector<TextureHandle> ColorAttachments;
  TextureHandle DepthAttachment = 0;
  std::string DebugName;
};

enum class LoadAction : uint32_t { Load, Clear, DontCare };
enum class StoreAction : uint32_t { Store, DontCare };

struct RenderPassDesc {
  // Zero selects the renderer's presentation target.
  RenderTargetHandle Target = 0;
  LoadAction ColorLoad = LoadAction::Load;
  StoreAction ColorStore = StoreAction::Store;
  std::array<float, 4> ClearColor{0.0f, 0.0f, 0.0f, 1.0f};
  LoadAction DepthLoad = LoadAction::Clear;
  StoreAction DepthStore = StoreAction::Store;
  float ClearDepth = 1.0f;
  std::string DebugName;
};

enum class IndexType : uint32_t { UInt16, UInt32 };

class ENGINE_RENDERER_API GraphicsDevice {
public:
  ~GraphicsDevice();
  GraphicsDevice(const GraphicsDevice &) = delete;
  GraphicsDevice &operator=(const GraphicsDevice &) = delete;

  ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc &desc);
  ShaderModuleHandle
  CreateShaderPermutation(ShaderModuleHandle baseShader,
                          std::span<const ShaderDefine> additionalDefines);
  void DestroyShaderModule(ShaderModuleHandle shader);
  // Non-owning view; valid until the shader is destroyed or hot reloaded.
  ShaderReflectionView GetShaderReflection(ShaderModuleHandle shader) const;

  GraphicsPipelineHandle
  CreateGraphicsPipeline(const GraphicsPipelineDesc &desc);
  ComputePipelineHandle CreateComputePipeline(const ComputePipelineDesc &desc);
  void DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline);
  void DestroyComputePipeline(ComputePipelineHandle pipeline);

  BufferHandle CreateBuffer(const BufferDesc &desc,
                            std::span<const std::byte> initialData = {});
  bool UpdateBuffer(BufferHandle buffer, uint64_t offset,
                    std::span<const std::byte> data);
  void DestroyBuffer(BufferHandle buffer);

  TextureHandle CreateTexture(const TextureDesc &desc,
                              std::span<const std::byte> initialData = {});
  bool UpdateTexture(TextureHandle texture, uint32_t mipLevel,
                     std::span<const std::byte> data);
  void DestroyTexture(TextureHandle texture);

  SamplerHandle CreateSampler(const SamplerDesc &desc = {});
  void DestroySampler(SamplerHandle sampler);
  RenderTargetHandle CreateRenderTarget(const RenderTargetDesc &desc);
  void DestroyRenderTarget(RenderTargetHandle target);

  // Commands are recorded on the renderer thread and executed during the
  // next Renderer::RenderFrame immediately before UI/presentation.
  void BeginRenderPass(const RenderPassDesc &desc);
  void EndRenderPass();
  void BindGraphicsPipeline(GraphicsPipelineHandle pipeline);
  void BindComputePipeline(ComputePipelineHandle pipeline);
  void BindVertexBuffer(uint32_t binding, BufferHandle buffer,
                        uint64_t offset = 0);
  void BindIndexBuffer(BufferHandle buffer, IndexType type = IndexType::UInt32,
                       uint64_t offset = 0);
  void BindUniformBuffer(uint32_t set, uint32_t binding, BufferHandle buffer,
                         uint64_t offset = 0, uint64_t size = 0);
  void BindStorageBuffer(uint32_t set, uint32_t binding, BufferHandle buffer,
                         uint64_t offset = 0, uint64_t size = 0);
  void BindTexture(uint32_t set, uint32_t binding, TextureHandle texture,
                   SamplerHandle sampler = 0);
  void BindSampler(uint32_t set, uint32_t binding, SamplerHandle sampler);
  void BindStorageTexture(uint32_t set, uint32_t binding,
                          TextureHandle texture);
  void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
            uint32_t firstVertex = 0, uint32_t firstInstance = 0);
  void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                   uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                   uint32_t firstInstance = 0);
  void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1,
                uint32_t groupCountZ = 1);
  void ResetCommands();

  // Recompiles changed file-backed shaders, atomically keeps the previous
  // native objects on compile failure, and rebuilds dependent pipelines.
  uint32_t ReloadChangedShaders();
  std::string_view LastShaderError() const;

private:
  friend class Renderer;
  friend struct RendererGraphicsDeviceFactory;
  struct Impl;
  explicit GraphicsDevice(Impl *impl);
  Impl *m_impl = nullptr;
};

} // namespace rendering
