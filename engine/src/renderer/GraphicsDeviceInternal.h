#pragma once

#include "engine/backend/IRenderBackend.h"
#include "engine/renderer/GraphicsDevice.h"
#include "engine/renderer/Renderer.h"

#include <memory>

namespace rendering::detail {

class IGraphicsBackend {
public:
  virtual ~IGraphicsBackend() = default;
  virtual ShaderModuleHandle CreateShader(const ShaderModuleDesc &) = 0;
  virtual ShaderModuleHandle
      CreatePermutation(ShaderModuleHandle, std::span<const ShaderDefine>) = 0;
  virtual void DestroyShader(ShaderModuleHandle) = 0;
  virtual const ShaderReflection &Reflection(ShaderModuleHandle) const = 0;
  virtual GraphicsPipelineHandle
  CreateGraphicsPipeline(const GraphicsPipelineDesc &) = 0;
  virtual ComputePipelineHandle
  CreateComputePipeline(const ComputePipelineDesc &) = 0;
  virtual void DestroyGraphicsPipeline(GraphicsPipelineHandle) = 0;
  virtual void DestroyComputePipeline(ComputePipelineHandle) = 0;
  virtual BufferHandle CreateBuffer(const BufferDesc &,
                                    std::span<const std::byte>) = 0;
  virtual bool UpdateBuffer(BufferHandle, uint64_t,
                            std::span<const std::byte>) = 0;
  virtual void DestroyBuffer(BufferHandle) = 0;
  virtual TextureHandle CreateTexture(const TextureDesc &,
                                      std::span<const std::byte>) = 0;
  virtual bool UpdateTexture(TextureHandle, uint32_t,
                             std::span<const std::byte>) = 0;
  virtual void DestroyTexture(TextureHandle) = 0;
  virtual SamplerHandle CreateSampler(const SamplerDesc &) = 0;
  virtual void DestroySampler(SamplerHandle) = 0;
  virtual RenderTargetHandle CreateRenderTarget(const RenderTargetDesc &) = 0;
  virtual void DestroyRenderTarget(RenderTargetHandle) = 0;
  virtual void BeginRenderPass(const RenderPassDesc &) = 0;
  virtual void EndRenderPass() = 0;
  virtual void BindGraphicsPipeline(GraphicsPipelineHandle) = 0;
  virtual void BindComputePipeline(ComputePipelineHandle) = 0;
  virtual void BindVertexBuffer(uint32_t, BufferHandle, uint64_t) = 0;
  virtual void BindIndexBuffer(BufferHandle, IndexType, uint64_t) = 0;
  virtual void BindUniformBuffer(uint32_t, uint32_t, BufferHandle, uint64_t,
                                 uint64_t) = 0;
  virtual void BindStorageBuffer(uint32_t, uint32_t, BufferHandle, uint64_t,
                                 uint64_t) = 0;
  virtual void BindTexture(uint32_t, uint32_t, TextureHandle,
                           SamplerHandle) = 0;
  virtual void BindSampler(uint32_t, uint32_t, SamplerHandle) = 0;
  virtual void BindStorageTexture(uint32_t, uint32_t, TextureHandle) = 0;
  virtual void Draw(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
  virtual void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) = 0;
  virtual void Dispatch(uint32_t, uint32_t, uint32_t) = 0;
  virtual void ResetCommands() = 0;
  virtual uint32_t ReloadChangedShaders() = 0;
  virtual std::string_view LastShaderError() const = 0;
  virtual void Execute(const engine::NativeCustomRenderContext &) = 0;
};

std::unique_ptr<IGraphicsBackend> CreateOpenGlGraphicsBackend();
#if ENGINE_HAS_VULKAN
std::unique_ptr<IGraphicsBackend>
CreateVulkanGraphicsBackend(const engine::NativeGraphicsContext &context);
#endif

} // namespace rendering::detail

namespace rendering {

struct RendererGraphicsDeviceFactory {
  static std::unique_ptr<GraphicsDevice>
  Create(Backend backend, const engine::NativeGraphicsContext &context,
         engine::IRenderBackend &renderBackend);
};

} // namespace rendering
