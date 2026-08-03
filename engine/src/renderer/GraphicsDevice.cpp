#include "GraphicsDeviceInternal.h"

#include "engine/renderer/Renderer.h"

#include <stdexcept>
#include <utility>

namespace rendering {

struct GraphicsDevice::Impl {
  std::unique_ptr<detail::IGraphicsBackend> Backend;
  engine::IRenderBackend *RenderBackend = nullptr;

  ~Impl() {
    if (RenderBackend) {
      RenderBackend->SetCustomRenderCallback({});
      RenderBackend->WaitIdle();
    }
    Backend.reset();
  }
};

GraphicsDevice::GraphicsDevice(Impl *impl) : m_impl(impl) {}
GraphicsDevice::~GraphicsDevice() { delete m_impl; }

namespace {
detail::IGraphicsBackend &Require(detail::IGraphicsBackend *backend) {
  if (!backend)
    throw std::runtime_error("GraphicsDevice is not initialized");
  return *backend;
}
} // namespace

#define RE_BACKEND Require(m_impl ? m_impl->Backend.get() : nullptr)
ShaderModuleHandle
GraphicsDevice::CreateShaderModule(const ShaderModuleDesc &d) {
  return RE_BACKEND.CreateShader(d);
}
ShaderModuleHandle
GraphicsDevice::CreateShaderPermutation(ShaderModuleHandle h,
                                        std::span<const ShaderDefine> d) {
  return RE_BACKEND.CreatePermutation(h, d);
}
void GraphicsDevice::DestroyShaderModule(ShaderModuleHandle h) {
  RE_BACKEND.DestroyShader(h);
}
ShaderReflectionView
GraphicsDevice::GetShaderReflection(ShaderModuleHandle h) const {
  const ShaderReflection &r = RE_BACKEND.Reflection(h);
  return {r.Stage, r.EntryPoint, r.Resources, r.PermutationKey, r.Generation};
}
GraphicsPipelineHandle
GraphicsDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc &d) {
  return RE_BACKEND.CreateGraphicsPipeline(d);
}
ComputePipelineHandle
GraphicsDevice::CreateComputePipeline(const ComputePipelineDesc &d) {
  return RE_BACKEND.CreateComputePipeline(d);
}
void GraphicsDevice::DestroyGraphicsPipeline(GraphicsPipelineHandle h) {
  RE_BACKEND.DestroyGraphicsPipeline(h);
}
void GraphicsDevice::DestroyComputePipeline(ComputePipelineHandle h) {
  RE_BACKEND.DestroyComputePipeline(h);
}
BufferHandle GraphicsDevice::CreateBuffer(const BufferDesc &d,
                                          std::span<const std::byte> b) {
  return RE_BACKEND.CreateBuffer(d, b);
}
bool GraphicsDevice::UpdateBuffer(BufferHandle h, uint64_t o,
                                  std::span<const std::byte> b) {
  return RE_BACKEND.UpdateBuffer(h, o, b);
}
void GraphicsDevice::DestroyBuffer(BufferHandle h) {
  RE_BACKEND.DestroyBuffer(h);
}
TextureHandle GraphicsDevice::CreateTexture(const TextureDesc &d,
                                            std::span<const std::byte> b) {
  return RE_BACKEND.CreateTexture(d, b);
}
bool GraphicsDevice::UpdateTexture(TextureHandle h, uint32_t m,
                                   std::span<const std::byte> b) {
  return RE_BACKEND.UpdateTexture(h, m, b);
}
void GraphicsDevice::DestroyTexture(TextureHandle h) {
  RE_BACKEND.DestroyTexture(h);
}
SamplerHandle GraphicsDevice::CreateSampler(const SamplerDesc &d) {
  return RE_BACKEND.CreateSampler(d);
}
void GraphicsDevice::DestroySampler(SamplerHandle h) {
  RE_BACKEND.DestroySampler(h);
}
RenderTargetHandle
GraphicsDevice::CreateRenderTarget(const RenderTargetDesc &d) {
  return RE_BACKEND.CreateRenderTarget(d);
}
void GraphicsDevice::DestroyRenderTarget(RenderTargetHandle h) {
  RE_BACKEND.DestroyRenderTarget(h);
}
void GraphicsDevice::BeginRenderPass(const RenderPassDesc &d) {
  RE_BACKEND.BeginRenderPass(d);
}
void GraphicsDevice::EndRenderPass() { RE_BACKEND.EndRenderPass(); }
void GraphicsDevice::BindGraphicsPipeline(GraphicsPipelineHandle h) {
  RE_BACKEND.BindGraphicsPipeline(h);
}
void GraphicsDevice::BindComputePipeline(ComputePipelineHandle h) {
  RE_BACKEND.BindComputePipeline(h);
}
void GraphicsDevice::BindVertexBuffer(uint32_t b, BufferHandle h, uint64_t o) {
  RE_BACKEND.BindVertexBuffer(b, h, o);
}
void GraphicsDevice::BindIndexBuffer(BufferHandle h, IndexType t, uint64_t o) {
  RE_BACKEND.BindIndexBuffer(h, t, o);
}
void GraphicsDevice::BindUniformBuffer(uint32_t s, uint32_t b, BufferHandle h,
                                       uint64_t o, uint64_t z) {
  RE_BACKEND.BindUniformBuffer(s, b, h, o, z);
}
void GraphicsDevice::BindStorageBuffer(uint32_t s, uint32_t b, BufferHandle h,
                                       uint64_t o, uint64_t z) {
  RE_BACKEND.BindStorageBuffer(s, b, h, o, z);
}
void GraphicsDevice::BindTexture(uint32_t s, uint32_t b, TextureHandle h,
                                 SamplerHandle p) {
  RE_BACKEND.BindTexture(s, b, h, p);
}
void GraphicsDevice::BindSampler(uint32_t s, uint32_t b, SamplerHandle p) {
  RE_BACKEND.BindSampler(s, b, p);
}
void GraphicsDevice::BindStorageTexture(uint32_t s, uint32_t b,
                                        TextureHandle h) {
  RE_BACKEND.BindStorageTexture(s, b, h);
}
void GraphicsDevice::Draw(uint32_t v, uint32_t i, uint32_t f, uint32_t n) {
  RE_BACKEND.Draw(v, i, f, n);
}
void GraphicsDevice::DrawIndexed(uint32_t c, uint32_t i, uint32_t f, int32_t v,
                                 uint32_t n) {
  RE_BACKEND.DrawIndexed(c, i, f, v, n);
}
void GraphicsDevice::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
  RE_BACKEND.Dispatch(x, y, z);
}
void GraphicsDevice::ResetCommands() { RE_BACKEND.ResetCommands(); }
uint32_t GraphicsDevice::ReloadChangedShaders() {
  return RE_BACKEND.ReloadChangedShaders();
}
std::string_view GraphicsDevice::LastShaderError() const {
  return RE_BACKEND.LastShaderError();
}
#undef RE_BACKEND

std::unique_ptr<GraphicsDevice> RendererGraphicsDeviceFactory::Create(
    Backend backend, const engine::NativeGraphicsContext &context,
    engine::IRenderBackend &renderBackend) {
  auto impl = std::make_unique<GraphicsDevice::Impl>();
  impl->RenderBackend = &renderBackend;
  if (backend == Backend::OpenGL)
    impl->Backend = detail::CreateOpenGlGraphicsBackend();
#if ENGINE_HAS_VULKAN
  else
    impl->Backend = detail::CreateVulkanGraphicsBackend(context);
#else
  else
    throw std::runtime_error("Vulkan GraphicsDevice was not built");
#endif
  auto device =
      std::unique_ptr<GraphicsDevice>(new GraphicsDevice(impl.release()));
  GraphicsDevice *raw = device.get();
  renderBackend.SetCustomRenderCallback(
      [raw](const engine::NativeCustomRenderContext &frame) {
        if (raw && raw->m_impl && raw->m_impl->Backend)
          raw->m_impl->Backend->Execute(frame);
      });
  return device;
}

} // namespace rendering
