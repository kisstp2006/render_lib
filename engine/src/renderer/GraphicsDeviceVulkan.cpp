#include "GraphicsDeviceInternal.h"
#include "ShaderCompilerCommon.h"

#include "engine/backend/vk/VulkanResources.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rendering::detail {
namespace {

constexpr uint32_t kDescriptorFrames = 4;

VkShaderStageFlagBits VkStage(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return VK_SHADER_STAGE_VERTEX_BIT;
  case ShaderStage::Fragment:
    return VK_SHADER_STAGE_FRAGMENT_BIT;
  case ShaderStage::Compute:
    return VK_SHADER_STAGE_COMPUTE_BIT;
  }
  throw std::runtime_error("Vulkan: invalid shader stage");
}
VkFormat VkTextureFormat(TextureFormat f) {
  switch (f) {
  case TextureFormat::R8Unorm:
    return VK_FORMAT_R8_UNORM;
  case TextureFormat::RG8Unorm:
    return VK_FORMAT_R8G8_UNORM;
  case TextureFormat::RGBA8Unorm:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case TextureFormat::RGBA8Srgb:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case TextureFormat::RGBA16Float:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case TextureFormat::R32Float:
    return VK_FORMAT_R32_SFLOAT;
  case TextureFormat::Depth32Float:
    return VK_FORMAT_D32_SFLOAT;
  }
  return VK_FORMAT_UNDEFINED;
}
VkFormat VkVertexFormat(VertexFormat f) {
  switch (f) {
  case VertexFormat::Float:
    return VK_FORMAT_R32_SFLOAT;
  case VertexFormat::Float2:
    return VK_FORMAT_R32G32_SFLOAT;
  case VertexFormat::Float3:
    return VK_FORMAT_R32G32B32_SFLOAT;
  case VertexFormat::Float4:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  case VertexFormat::UInt:
    return VK_FORMAT_R32_UINT;
  case VertexFormat::UInt2:
    return VK_FORMAT_R32G32_UINT;
  case VertexFormat::UInt4:
    return VK_FORMAT_R32G32B32A32_UINT;
  case VertexFormat::UByte4Normalized:
    return VK_FORMAT_R8G8B8A8_UNORM;
  }
  return VK_FORMAT_UNDEFINED;
}
VkPrimitiveTopology VkTopology(PrimitiveTopology t) {
  switch (t) {
  case PrimitiveTopology::TriangleList:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  case PrimitiveTopology::TriangleStrip:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  case PrimitiveTopology::LineList:
    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  case PrimitiveTopology::PointList:
    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  }
  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}
VkCompareOp VkCompare(CompareOperation o) {
  constexpr VkCompareOp v[] = {VK_COMPARE_OP_NEVER,
                               VK_COMPARE_OP_LESS,
                               VK_COMPARE_OP_LESS_OR_EQUAL,
                               VK_COMPARE_OP_EQUAL,
                               VK_COMPARE_OP_GREATER_OR_EQUAL,
                               VK_COMPARE_OP_GREATER,
                               VK_COMPARE_OP_ALWAYS};
  return v[static_cast<size_t>(o)];
}
VkDescriptorType DescriptorType(ShaderResourceType t) {
  switch (t) {
  case ShaderResourceType::UniformBuffer:
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case ShaderResourceType::StorageBuffer:
    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  case ShaderResourceType::SampledTexture:
    return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  case ShaderResourceType::CombinedTextureSampler:
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  case ShaderResourceType::StorageTexture:
    return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  case ShaderResourceType::Sampler:
    return VK_DESCRIPTOR_TYPE_SAMPLER;
  default:
    break;
  }
  return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}
uint64_t BindingKey(uint32_t set, uint32_t binding) {
  return (static_cast<uint64_t>(set) << 32u) | binding;
}

template <class Map> auto &Find(Map &map, uint64_t h, const char *type) {
  auto i = map.find(h);
  if (i == map.end())
    throw std::runtime_error(std::string("Vulkan: invalid ") + type +
                             " handle");
  return i->second;
}

struct DescriptorState {
  std::vector<VkDescriptorSetLayout> Layouts;
  VkDescriptorPool Pool = VK_NULL_HANDLE;
  std::array<std::vector<VkDescriptorSet>, kDescriptorFrames> Sets;
  std::unordered_map<uint64_t, VkDescriptorType> Types;
};

class VulkanGraphicsBackend final : public IGraphicsBackend {
  struct Shader {
    CompiledShader Compiled;
    VkShaderModule Native = VK_NULL_HANDLE;
  };
  struct PipelineBase {
    VkPipeline Native = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    DescriptorState Descriptors;
  };
  struct GraphicsPipeline : PipelineBase {
    GraphicsPipelineDesc Desc;
  };
  struct ComputePipeline : PipelineBase {
    ComputePipelineDesc Desc;
  };
  struct Buffer {
    BufferDesc Desc;
    engine::vulkan::Buffer Native;
  };
  struct Texture {
    TextureDesc Desc;
    engine::vulkan::Image Native;
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
  };
  struct Sampler {
    SamplerDesc Desc;
    VkSampler Native = VK_NULL_HANDLE;
  };
  struct RenderTarget {
    RenderTargetDesc Desc;
    uint32_t Width = 0, Height = 0;
  };
  enum class Op {
    BeginPass,
    EndPass,
    BindGraphics,
    BindCompute,
    VertexBuffer,
    IndexBuffer,
    UniformBuffer,
    StorageBuffer,
    Texture,
    SamplerBinding,
    StorageTexture,
    Draw,
    DrawIndexed,
    Dispatch
  };
  struct Command {
    Op Type{};
    RenderPassDesc Pass;
    uint64_t A = 0, B = 0, C = 0, D = 0, E = 0;
    int32_t Signed = 0;
  };

public:
  explicit VulkanGraphicsBackend(const engine::NativeGraphicsContext &c)
      : m_physical(reinterpret_cast<VkPhysicalDevice>(c.PhysicalDevice)),
        m_device(reinterpret_cast<VkDevice>(c.Device)),
        m_queue(reinterpret_cast<VkQueue>(c.Queue)),
        m_queueFamily(c.QueueFamily),
        m_swapchainFormat(static_cast<VkFormat>(c.ColorFormat)) {
    if (!m_physical || !m_device || !m_queue)
      throw std::runtime_error(
          "Vulkan: incomplete native GraphicsDevice context");
    m_allocator.Init(m_physical, m_device, m_queueFamily, m_queueFamily);
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = m_queueFamily;
    if (vkCreateCommandPool(m_device, &info, nullptr, &m_uploadPool) !=
        VK_SUCCESS)
      throw std::runtime_error("Vulkan: failed to create SDK upload pool");
  }
  ~VulkanGraphicsBackend() override {
    vkDeviceWaitIdle(m_device);
    for (auto &[_, p] : m_graphics)
      DestroyPipeline(p);
    for (auto &[_, p] : m_compute)
      DestroyPipeline(p);
    for (auto &[_, s] : m_shaders)
      vkDestroyShaderModule(m_device, s.Native, nullptr);
    for (auto &[_, b] : m_buffers)
      m_allocator.Destroy(b.Native);
    for (auto &[_, t] : m_textures)
      m_allocator.Destroy(t.Native);
    for (auto &[_, s] : m_samplers)
      vkDestroySampler(m_device, s.Native, nullptr);
    vkDestroyCommandPool(m_device, m_uploadPool, nullptr);
  }

  ShaderModuleHandle CreateShader(const ShaderModuleDesc &d) override {
    CompiledShader c = CompileShader(d, engine::shader::SpirvTarget::Vulkan13);
    VkShaderModule n = CreateModule(c.Spirv);
    uint64_t h = Next();
    m_permutations[c.Reflection.PermutationKey] = h;
    m_shaders.emplace(h, Shader{std::move(c), n});
    return h;
  }
  ShaderModuleHandle
  CreatePermutation(ShaderModuleHandle base,
                    std::span<const ShaderDefine> defs) override {
    ShaderModuleDesc d = Find(m_shaders, base, "shader").Compiled.Desc;
    d.Defines.insert(d.Defines.end(), defs.begin(), defs.end());
    CompiledShader c = CompileShader(d, engine::shader::SpirvTarget::Vulkan13);
    if (auto i = m_permutations.find(c.Reflection.PermutationKey);
        i != m_permutations.end())
      return i->second;
    VkShaderModule n = CreateModule(c.Spirv);
    uint64_t h = Next();
    m_permutations[c.Reflection.PermutationKey] = h;
    m_shaders.emplace(h, Shader{std::move(c), n});
    return h;
  }
  void DestroyShader(ShaderModuleHandle h) override {
    auto i = m_shaders.find(h);
    if (i == m_shaders.end())
      return;
    vkDestroyShaderModule(m_device, i->second.Native, nullptr);
    m_permutations.erase(i->second.Compiled.Reflection.PermutationKey);
    m_shaders.erase(i);
  }
  const ShaderReflection &Reflection(ShaderModuleHandle h) const override {
    return Find(m_shaders, h, "shader").Compiled.Reflection;
  }

  GraphicsPipelineHandle
  CreateGraphicsPipeline(const GraphicsPipelineDesc &d) override {
    GraphicsPipeline p = BuildGraphics(d);
    uint64_t h = Next();
    m_graphics.emplace(h, std::move(p));
    return h;
  }
  ComputePipelineHandle
  CreateComputePipeline(const ComputePipelineDesc &d) override {
    ComputePipeline p = BuildCompute(d);
    uint64_t h = Next();
    m_compute.emplace(h, std::move(p));
    return h;
  }
  void DestroyGraphicsPipeline(GraphicsPipelineHandle h) override {
    auto i = m_graphics.find(h);
    if (i != m_graphics.end()) {
      WaitForResourceDestruction();
      DestroyPipeline(i->second);
      m_graphics.erase(i);
    }
  }
  void DestroyComputePipeline(ComputePipelineHandle h) override {
    auto i = m_compute.find(h);
    if (i != m_compute.end()) {
      WaitForResourceDestruction();
      DestroyPipeline(i->second);
      m_compute.erase(i);
    }
  }

  BufferHandle CreateBuffer(const BufferDesc &d,
                            std::span<const std::byte> data) override {
    if (!d.Size || data.size() > d.Size)
      throw std::runtime_error("Vulkan: invalid buffer size");
    VkBufferUsageFlags usage = 0;
    const uint32_t u = static_cast<uint32_t>(d.Usage);
    if (u & static_cast<uint32_t>(BufferUsage::Vertex))
      usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (u & static_cast<uint32_t>(BufferUsage::Index))
      usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (u & static_cast<uint32_t>(BufferUsage::Uniform))
      usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (u & static_cast<uint32_t>(BufferUsage::Storage))
      usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (u & static_cast<uint32_t>(BufferUsage::TransferSource))
      usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (u & static_cast<uint32_t>(BufferUsage::TransferDestination))
      usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Buffer b{
        d, m_allocator.CreateBuffer(d.Size, usage,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    if (!data.empty())
      WriteMemory(b.Native.Memory, 0, data);
    uint64_t h = Next();
    m_buffers.emplace(h, std::move(b));
    return h;
  }
  bool UpdateBuffer(BufferHandle h, uint64_t o,
                    std::span<const std::byte> data) override {
    auto &b = Find(m_buffers, h, "buffer");
    if (o + data.size() > b.Desc.Size)
      return false;
    WriteMemory(b.Native.Memory, o, data);
    return true;
  }
  void DestroyBuffer(BufferHandle h) override {
    auto i = m_buffers.find(h);
    if (i != m_buffers.end()) {
      WaitForResourceDestruction();
      m_allocator.Destroy(i->second.Native);
      m_buffers.erase(i);
    }
  }

  TextureHandle CreateTexture(const TextureDesc &d,
                              std::span<const std::byte> data) override {
    if (!d.Width || !d.Height || !d.MipLevels)
      throw std::runtime_error("Vulkan: invalid texture size");
    VkImageUsageFlags usage = 0;
    const uint32_t u = static_cast<uint32_t>(d.Usage);
    if (u & static_cast<uint32_t>(TextureUsage::Sampled))
      usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (u & static_cast<uint32_t>(TextureUsage::Storage))
      usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (u & static_cast<uint32_t>(TextureUsage::ColorAttachment))
      usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (u & static_cast<uint32_t>(TextureUsage::DepthAttachment))
      usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (u & static_cast<uint32_t>(TextureUsage::TransferSource))
      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (u & static_cast<uint32_t>(TextureUsage::TransferDestination) ||
        !data.empty())
      usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const bool depth = d.Format == TextureFormat::Depth32Float;
    Texture t{d, m_allocator.CreateImage2D(d.Width, d.Height,
                                           VkTextureFormat(d.Format), usage,
                                           depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                 : VK_IMAGE_ASPECT_COLOR_BIT,
                                           d.MipLevels)};
    if (!data.empty())
      UploadTexture(t, 0, data);
    uint64_t h = Next();
    m_textures.emplace(h, std::move(t));
    return h;
  }
  bool UpdateTexture(TextureHandle h, uint32_t mip,
                     std::span<const std::byte> data) override {
    auto &t = Find(m_textures, h, "texture");
    if (mip >= t.Desc.MipLevels)
      return false;
    vkQueueWaitIdle(m_queue);
    UploadTexture(t, mip, data);
    return true;
  }
  void DestroyTexture(TextureHandle h) override {
    auto i = m_textures.find(h);
    if (i != m_textures.end()) {
      WaitForResourceDestruction();
      m_allocator.Destroy(i->second.Native);
      m_textures.erase(i);
    }
  }

  SamplerHandle CreateSampler(const SamplerDesc &d) override {
    VkSamplerCreateInfo i{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    i.minFilter =
        d.MinFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    i.magFilter =
        d.MagFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    auto address = [](AddressMode m) {
      switch (m) {
      case AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
      case AddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      case AddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      case AddressMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
      }
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };
    i.addressModeU = address(d.AddressU);
    i.addressModeV = address(d.AddressV);
    i.addressModeW = address(d.AddressW);
    i.maxLod = VK_LOD_CLAMP_NONE;
    i.anisotropyEnable = d.MaxAnisotropy > 1.0f;
    i.maxAnisotropy = d.MaxAnisotropy;
    Sampler s{d};
    if (vkCreateSampler(m_device, &i, nullptr, &s.Native) != VK_SUCCESS)
      throw std::runtime_error("Vulkan: sampler creation failed");
    uint64_t h = Next();
    m_samplers.emplace(h, s);
    return h;
  }
  void DestroySampler(SamplerHandle h) override {
    auto i = m_samplers.find(h);
    if (i != m_samplers.end()) {
      WaitForResourceDestruction();
      vkDestroySampler(m_device, i->second.Native, nullptr);
      m_samplers.erase(i);
    }
  }

  RenderTargetHandle CreateRenderTarget(const RenderTargetDesc &d) override {
    if (d.ColorAttachments.empty() && !d.DepthAttachment)
      throw std::runtime_error("Vulkan: empty render target");
    RenderTarget r;
    r.Desc = d;
    auto size = [&](TextureHandle h) {
      auto &t = Find(m_textures, h, "texture");
      if (!r.Width) {
        r.Width = t.Desc.Width;
        r.Height = t.Desc.Height;
      } else if (r.Width != t.Desc.Width || r.Height != t.Desc.Height)
        throw std::runtime_error("Vulkan: render target size mismatch");
    };
    for (auto h : d.ColorAttachments)
      size(h);
    if (d.DepthAttachment)
      size(d.DepthAttachment);
    uint64_t h = Next();
    m_targets.emplace(h, std::move(r));
    return h;
  }
  void DestroyRenderTarget(RenderTargetHandle h) override {
    m_targets.erase(h);
  }

  void BeginRenderPass(const RenderPassDesc &d) override {
    Command c;
    c.Type = Op::BeginPass;
    c.Pass = d;
    m_commands.push_back(std::move(c));
  }
  void EndRenderPass() override { m_commands.push_back({Op::EndPass}); }
  void BindGraphicsPipeline(GraphicsPipelineHandle h) override {
    Push(Op::BindGraphics, h);
  }
  void BindComputePipeline(ComputePipelineHandle h) override {
    Push(Op::BindCompute, h);
  }
  void BindVertexBuffer(uint32_t b, BufferHandle h, uint64_t o) override {
    Push(Op::VertexBuffer, b, h, o);
  }
  void BindIndexBuffer(BufferHandle h, IndexType t, uint64_t o) override {
    Push(Op::IndexBuffer, h, static_cast<uint64_t>(t), o);
  }
  void BindUniformBuffer(uint32_t s, uint32_t b, BufferHandle h, uint64_t o,
                         uint64_t z) override {
    Push(Op::UniformBuffer, s, b, h, o, z);
  }
  void BindStorageBuffer(uint32_t s, uint32_t b, BufferHandle h, uint64_t o,
                         uint64_t z) override {
    Push(Op::StorageBuffer, s, b, h, o, z);
  }
  void BindTexture(uint32_t s, uint32_t b, TextureHandle h,
                   SamplerHandle p) override {
    Push(Op::Texture, s, b, h, p);
  }
  void BindSampler(uint32_t s, uint32_t b, SamplerHandle p) override {
    Push(Op::SamplerBinding, s, b, p);
  }
  void BindStorageTexture(uint32_t s, uint32_t b, TextureHandle h) override {
    Push(Op::StorageTexture, s, b, h);
  }
  void Draw(uint32_t v, uint32_t i, uint32_t f, uint32_t n) override {
    Push(Op::Draw, v, i, f, n);
  }
  void DrawIndexed(uint32_t c, uint32_t i, uint32_t f, int32_t v,
                   uint32_t n) override {
    Command q;
    q.Type = Op::DrawIndexed;
    q.A = c;
    q.B = i;
    q.C = f;
    q.Signed = v;
    q.D = n;
    m_commands.push_back(q);
  }
  void Dispatch(uint32_t x, uint32_t y, uint32_t z) override {
    Push(Op::Dispatch, x, y, z);
  }
  void ResetCommands() override { m_commands.clear(); }

  uint32_t ReloadChangedShaders() override {
    uint32_t changed = 0;
    m_lastError.clear();
    for (auto &[h, s] : m_shaders) {
      if (!ShaderFilesChanged(s.Compiled))
        continue;
      try {
        CompiledShader c = CompileShader(s.Compiled.Desc,
                                         engine::shader::SpirvTarget::Vulkan13);
        VkShaderModule module = CreateModule(c.Spirv);
        vkDeviceWaitIdle(m_device);
        VkShaderModule old = s.Native;
        s.Native = module;
        c.Reflection.Generation = s.Compiled.Reflection.Generation + 1;
        CompiledShader oldCompiled = std::move(s.Compiled);
        s.Compiled = std::move(c);
        try {
          for (auto &[_, p] : m_graphics)
            if (p.Desc.VertexShader == h || p.Desc.FragmentShader == h) {
              GraphicsPipeline replacement = BuildGraphics(p.Desc);
              DestroyPipeline(p);
              p = std::move(replacement);
            }
          for (auto &[_, p] : m_compute)
            if (p.Desc.ComputeShader == h) {
              ComputePipeline replacement = BuildCompute(p.Desc);
              DestroyPipeline(p);
              p = std::move(replacement);
            }
        } catch (...) {
          s.Native = old;
          s.Compiled = std::move(oldCompiled);
          vkDestroyShaderModule(m_device, module, nullptr);
          throw;
        }
        vkDestroyShaderModule(m_device, old, nullptr);
        ++changed;
      } catch (const std::exception &e) {
        m_lastError = e.what();
      }
    }
    return changed;
  }
  std::string_view LastShaderError() const override { return m_lastError; }

  void Execute(const engine::NativeCustomRenderContext &frame) override {
    VkCommandBuffer cmd =
        reinterpret_cast<VkCommandBuffer>(frame.CommandBuffer);
    const uint32_t slot = frame.FrameIndex % kDescriptorFrames;
    GraphicsPipeline *graphics = nullptr;
    ComputePipeline *compute = nullptr;
    PipelineBase *active = nullptr;
    RenderTarget *activeTarget = nullptr;
    bool rendering = false;
    for (const Command &c : m_commands) {
      switch (c.Type) {
      case Op::BeginPass: {
        if (rendering)
          throw std::runtime_error("Vulkan: nested render pass");
        std::vector<VkRenderingAttachmentInfo> colors;
        VkRenderingAttachmentInfo depth{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        uint32_t width = frame.Width, height = frame.Height;
        if (c.Pass.Target) {
          activeTarget = &Find(m_targets, c.Pass.Target, "render target");
          width = activeTarget->Width;
          height = activeTarget->Height;
          for (auto h : activeTarget->Desc.ColorAttachments) {
            auto &t = Find(m_textures, h, "texture");
            Transition(cmd, t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            colors.push_back(Attachment(t.Native.View, c.Pass.ColorLoad,
                                        c.Pass.ColorStore, c.Pass.ClearColor,
                                        c.Pass.ClearDepth, false));
          }
          if (activeTarget->Desc.DepthAttachment) {
            auto &t =
                Find(m_textures, activeTarget->Desc.DepthAttachment, "texture");
            Transition(cmd, t, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            depth =
                Attachment(t.Native.View, c.Pass.DepthLoad, c.Pass.DepthStore,
                           c.Pass.ClearColor, c.Pass.ClearDepth, true);
          }
        } else {
          activeTarget = nullptr;
          colors.push_back(
              Attachment(reinterpret_cast<VkImageView>(frame.ColorTarget),
                         c.Pass.ColorLoad, c.Pass.ColorStore, c.Pass.ClearColor,
                         c.Pass.ClearDepth, false));
        }
        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
        info.renderArea.extent = {width, height};
        info.layerCount = 1;
        info.colorAttachmentCount = static_cast<uint32_t>(colors.size());
        info.pColorAttachments = colors.data();
        if (activeTarget && activeTarget->Desc.DepthAttachment)
          info.pDepthAttachment = &depth;
        vkCmdBeginRendering(cmd, &info);
        VkViewport vp{
            0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        VkRect2D sc{{0, 0}, {width, height}};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        rendering = true;
        break;
      }
      case Op::EndPass:
        if (rendering) {
          vkCmdEndRendering(cmd);
          rendering = false;
          if (activeTarget) {
            for (auto h : activeTarget->Desc.ColorAttachments) {
              auto &t = Find(m_textures, h, "texture");
              const bool sampled = static_cast<uint32_t>(t.Desc.Usage) &
                                   static_cast<uint32_t>(TextureUsage::Sampled);
              if (sampled)
                Transition(cmd, t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            if (activeTarget->Desc.DepthAttachment) {
              auto &t = Find(m_textures, activeTarget->Desc.DepthAttachment,
                             "texture");
              const bool sampled = static_cast<uint32_t>(t.Desc.Usage) &
                                   static_cast<uint32_t>(TextureUsage::Sampled);
              if (sampled)
                Transition(cmd, t, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
            }
          }
        }
        break;
      case Op::BindGraphics:
        graphics = &Find(m_graphics, c.A, "graphics pipeline");
        compute = nullptr;
        active = graphics;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          graphics->Native);
        break;
      case Op::BindCompute:
        compute = &Find(m_compute, c.A, "compute pipeline");
        graphics = nullptr;
        active = compute;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute->Native);
        break;
      case Op::VertexBuffer: {
        auto &b = Find(m_buffers, c.B, "buffer");
        VkBuffer native = b.Native.Handle;
        VkDeviceSize offset = c.C;
        vkCmdBindVertexBuffers(cmd, static_cast<uint32_t>(c.A), 1, &native,
                               &offset);
        break;
      }
      case Op::IndexBuffer: {
        auto &b = Find(m_buffers, c.A, "buffer");
        vkCmdBindIndexBuffer(cmd, b.Native.Handle, c.C,
                             static_cast<IndexType>(c.B) == IndexType::UInt16
                                 ? VK_INDEX_TYPE_UINT16
                                 : VK_INDEX_TYPE_UINT32);
        break;
      }
      case Op::UniformBuffer:
        UpdateBufferDescriptor(active, slot, c,
                               VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        break;
      case Op::StorageBuffer:
        UpdateBufferDescriptor(active, slot, c,
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        break;
      case Op::Texture:
        UpdateTextureDescriptor(cmd, active, slot, c, false);
        break;
      case Op::SamplerBinding:
        UpdateSamplerDescriptor(active, slot, c);
        break;
      case Op::StorageTexture:
        UpdateTextureDescriptor(cmd, active, slot, c, true);
        break;
      case Op::Draw:
        if (!graphics)
          throw std::runtime_error("Vulkan: no graphics pipeline");
        BindDescriptorSets(cmd, *graphics, slot,
                           VK_PIPELINE_BIND_POINT_GRAPHICS);
        vkCmdDraw(cmd, static_cast<uint32_t>(c.A), static_cast<uint32_t>(c.B),
                  static_cast<uint32_t>(c.C), static_cast<uint32_t>(c.D));
        break;
      case Op::DrawIndexed:
        if (!graphics)
          throw std::runtime_error("Vulkan: no graphics pipeline");
        BindDescriptorSets(cmd, *graphics, slot,
                           VK_PIPELINE_BIND_POINT_GRAPHICS);
        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(c.A),
                         static_cast<uint32_t>(c.B), static_cast<uint32_t>(c.C),
                         c.Signed, static_cast<uint32_t>(c.D));
        break;
      case Op::Dispatch:
        if (!compute)
          throw std::runtime_error("Vulkan: no compute pipeline");
        BindDescriptorSets(cmd, *compute, slot, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdDispatch(cmd, static_cast<uint32_t>(c.A),
                      static_cast<uint32_t>(c.B), static_cast<uint32_t>(c.C));
        {
          VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
          barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
          barrier.dstAccessMask =
              VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
          VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
          dep.memoryBarrierCount = 1;
          dep.pMemoryBarriers = &barrier;
          vkCmdPipelineBarrier2(cmd, &dep);
        }
        break;
      }
    }
    if (rendering)
      vkCmdEndRendering(cmd);
    m_commands.clear();
  }

private:
  uint64_t Next() { return m_nextHandle++; }
  void Push(Op o, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0,
            uint64_t d = 0, uint64_t e = 0) {
    Command q;
    q.Type = o;
    q.A = a;
    q.B = b;
    q.C = c;
    q.D = d;
    q.E = e;
    m_commands.push_back(q);
  }
  void WaitForResourceDestruction() {
    if (vkDeviceWaitIdle(m_device) != VK_SUCCESS)
      throw std::runtime_error(
          "Vulkan: failed to wait for resource destruction");
  }
  VkShaderModule CreateModule(std::span<const uint32_t> w) {
    VkShaderModuleCreateInfo i{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    i.codeSize = w.size_bytes();
    i.pCode = w.data();
    VkShaderModule m;
    if (vkCreateShaderModule(m_device, &i, nullptr, &m) != VK_SUCCESS)
      throw std::runtime_error("Vulkan: shader module creation failed");
    return m;
  }
  void WriteMemory(VkDeviceMemory memory, uint64_t offset,
                   std::span<const std::byte> d) {
    void *p = nullptr;
    if (vkMapMemory(m_device, memory, offset, d.size(), 0, &p) != VK_SUCCESS)
      throw std::runtime_error("Vulkan: buffer map failed");
    std::memcpy(p, d.data(), d.size());
    vkUnmapMemory(m_device, memory);
  }

  DescriptorState
  CreateDescriptors(std::span<const ShaderModuleHandle> shaderHandles) {
    struct Binding {
      VkDescriptorType Type;
      uint32_t Count;
      VkShaderStageFlags Stages;
    };
    std::map<std::pair<uint32_t, uint32_t>, Binding> merged;
    for (auto h : shaderHandles) {
      auto &s = Find(m_shaders, h, "shader");
      for (const auto &r : s.Compiled.Reflection.Resources) {
        VkDescriptorType type = DescriptorType(r.Type);
        if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM)
          continue;
        auto key = std::pair{r.Set, r.Binding};
        auto it = merged.find(key);
        if (it == merged.end())
          merged.emplace(key, Binding{type, std::max(r.ArrayCount, 1u),
                                      static_cast<VkShaderStageFlags>(
                                          VkStage(s.Compiled.Desc.Stage))});
        else {
          if (it->second.Type != type)
            throw std::runtime_error(
                "Vulkan: incompatible reflected descriptor types");
          it->second.Stages |= VkStage(s.Compiled.Desc.Stage);
        }
      }
    }
    DescriptorState state;
    if (merged.empty())
      return state;
    const uint32_t maxSet = merged.rbegin()->first.first;
    state.Layouts.resize(maxSet + 1, VK_NULL_HANDLE);
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> perSet(maxSet + 1);
    std::map<VkDescriptorType, uint32_t> counts;
    for (const auto &[key, b] : merged) {
      perSet[key.first].push_back(
          {key.second, b.Type, b.Count, b.Stages, nullptr});
      state.Types[BindingKey(key.first, key.second)] = b.Type;
      counts[b.Type] += b.Count * kDescriptorFrames;
    }
    for (uint32_t set = 0; set <= maxSet; ++set) {
      VkDescriptorSetLayoutCreateInfo i{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      i.bindingCount = static_cast<uint32_t>(perSet[set].size());
      i.pBindings = perSet[set].data();
      if (vkCreateDescriptorSetLayout(m_device, &i, nullptr,
                                      &state.Layouts[set]) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: descriptor layout creation failed");
    }
    std::vector<VkDescriptorPoolSize> sizes;
    for (auto [type, count] : counts)
      sizes.push_back({type, count});
    VkDescriptorPoolCreateInfo pi{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets =
        static_cast<uint32_t>(state.Layouts.size()) * kDescriptorFrames;
    pi.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pi.pPoolSizes = sizes.data();
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &state.Pool) !=
        VK_SUCCESS)
      throw std::runtime_error("Vulkan: descriptor pool creation failed");
    for (uint32_t frame = 0; frame < kDescriptorFrames; ++frame) {
      state.Sets[frame].resize(state.Layouts.size());
      VkDescriptorSetAllocateInfo ai{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      ai.descriptorPool = state.Pool;
      ai.descriptorSetCount = static_cast<uint32_t>(state.Layouts.size());
      ai.pSetLayouts = state.Layouts.data();
      if (vkAllocateDescriptorSets(m_device, &ai, state.Sets[frame].data()) !=
          VK_SUCCESS)
        throw std::runtime_error("Vulkan: descriptor allocation failed");
    }
    return state;
  }
  VkPipelineLayout CreateLayout(const DescriptorState &d) {
    VkPipelineLayoutCreateInfo i{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    i.setLayoutCount = static_cast<uint32_t>(d.Layouts.size());
    i.pSetLayouts = d.Layouts.data();
    VkPipelineLayout l;
    if (vkCreatePipelineLayout(m_device, &i, nullptr, &l) != VK_SUCCESS)
      throw std::runtime_error("Vulkan: pipeline layout creation failed");
    return l;
  }
  GraphicsPipeline BuildGraphics(const GraphicsPipelineDesc &d) {
    if (!d.VertexShader || !d.FragmentShader)
      throw std::runtime_error("Vulkan: graphics pipeline requires shaders");
    GraphicsPipeline p;
    p.Desc = d;
    ShaderModuleHandle hs[] = {d.VertexShader, d.FragmentShader};
    p.Descriptors = CreateDescriptors(hs);
    p.Layout = CreateLayout(p.Descriptors);
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int n = 0; n < 2; ++n) {
      auto &s = Find(m_shaders, hs[n], "shader");
      stages[n].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[n].stage = VkStage(s.Compiled.Desc.Stage);
      stages[n].module = s.Native;
      stages[n].pName = s.Compiled.Desc.EntryPoint.c_str();
    }
    std::vector<VkVertexInputBindingDescription> bindings;
    for (auto &b : d.VertexBindings)
      bindings.push_back({b.Binding, b.Stride,
                          b.InputRate == VertexInputRate::PerInstance
                              ? VK_VERTEX_INPUT_RATE_INSTANCE
                              : VK_VERTEX_INPUT_RATE_VERTEX});
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (auto &a : d.VertexAttributes)
      attributes.push_back(
          {a.Location, a.Binding, VkVertexFormat(a.Format), a.Offset});
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vi.pVertexBindingDescriptions = bindings.data();
    vi.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributes.size());
    vi.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VkTopology(d.Topology);
    VkPipelineViewportStateCreateInfo vp{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = d.Cull == CullMode::None
                      ? VK_CULL_MODE_NONE
                      : (d.Cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT
                                                  : VK_CULL_MODE_FRONT_BIT);
    rs.frontFace = d.Winding == FrontFace::CounterClockwise
                       ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                       : VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = d.DepthTest;
    ds.depthWriteEnable = d.DepthWrite;
    ds.depthCompareOp = VkCompare(d.DepthCompare);
    std::vector<VkFormat> colors;
    if (d.ColorFormats.empty())
      colors.push_back(m_swapchainFormat);
    else
      for (auto f : d.ColorFormats)
        colors.push_back(VkTextureFormat(f));
    std::vector<VkPipelineColorBlendAttachmentState> blend(colors.size());
    for (auto &b : blend) {
      b.colorWriteMask = 0xf;
      b.blendEnable = d.Blend.Enabled;
      if (b.blendEnable) {
        b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        b.colorBlendOp = VK_BLEND_OP_ADD;
        b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        b.alphaBlendOp = VK_BLEND_OP_ADD;
      }
    }
    VkPipelineColorBlendStateCreateInfo bs{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = static_cast<uint32_t>(blend.size());
    bs.pAttachments = blend.data();
    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT,
                             VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyns;
    VkPipelineRenderingCreateInfo rendering{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = static_cast<uint32_t>(colors.size());
    rendering.pColorAttachmentFormats = colors.data();
    rendering.depthAttachmentFormat =
        d.HasDepthFormat ? VkTextureFormat(d.DepthFormat) : VK_FORMAT_UNDEFINED;
    VkGraphicsPipelineCreateInfo info{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &bs;
    info.pDynamicState = &dyn;
    info.layout = p.Layout;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &p.Native) != VK_SUCCESS) {
      DestroyPipeline(p);
      throw std::runtime_error("Vulkan: graphics pipeline creation failed");
    }
    return p;
  }
  ComputePipeline BuildCompute(const ComputePipelineDesc &d) {
    ComputePipeline p;
    p.Desc = d;
    ShaderModuleHandle hs[] = {d.ComputeShader};
    p.Descriptors = CreateDescriptors(hs);
    p.Layout = CreateLayout(p.Descriptors);
    auto &s = Find(m_shaders, d.ComputeShader, "shader");
    VkComputePipelineCreateInfo i{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    i.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    i.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    i.stage.module = s.Native;
    i.stage.pName = s.Compiled.Desc.EntryPoint.c_str();
    i.layout = p.Layout;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &i, nullptr,
                                 &p.Native) != VK_SUCCESS) {
      DestroyPipeline(p);
      throw std::runtime_error("Vulkan: compute pipeline creation failed");
    }
    return p;
  }
  template <class P> void DestroyPipeline(P &p) {
    if (p.Native)
      vkDestroyPipeline(m_device, p.Native, nullptr);
    if (p.Layout)
      vkDestroyPipelineLayout(m_device, p.Layout, nullptr);
    if (p.Descriptors.Pool)
      vkDestroyDescriptorPool(m_device, p.Descriptors.Pool, nullptr);
    for (auto l : p.Descriptors.Layouts)
      vkDestroyDescriptorSetLayout(m_device, l, nullptr);
    p.Native = VK_NULL_HANDLE;
    p.Layout = VK_NULL_HANDLE;
    p.Descriptors = {};
  }

  VkCommandBuffer BeginImmediate() {
    VkCommandBufferAllocateInfo ai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = m_uploadPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer c;
    if (vkAllocateCommandBuffers(m_device, &ai, &c) != VK_SUCCESS)
      throw std::runtime_error("Vulkan: upload command allocation failed");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(c, &bi);
    return c;
  }
  void EndImmediate(VkCommandBuffer c) {
    vkEndCommandBuffer(c);
    VkCommandBufferSubmitInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    ci.commandBuffer = c;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &ci;
    if (vkQueueSubmit2(m_queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS)
      throw std::runtime_error("Vulkan: upload submit failed");
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_uploadPool, 1, &c);
  }
  void UploadTexture(Texture &t, uint32_t mip,
                     std::span<const std::byte> data) {
    auto staging =
        m_allocator.CreateBuffer(data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    WriteMemory(staging.Memory, 0, data);
    VkCommandBuffer c = BeginImmediate();
    Transition(c, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask =
        t.Desc.Format == TextureFormat::Depth32Float
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = mip;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {std::max(1u, t.Desc.Width >> mip),
                        std::max(1u, t.Desc.Height >> mip), 1};
    vkCmdCopyBufferToImage(c, staging.Handle, t.Native.Handle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    Transition(c, t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EndImmediate(c);
    m_allocator.Destroy(staging);
  }
  void Transition(VkCommandBuffer c, Texture &t, VkImageLayout next) {
    if (t.Layout == next)
      return;
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask =
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask =
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = t.Layout;
    b.newLayout = next;
    b.image = t.Native.Handle;
    b.subresourceRange.aspectMask = t.Desc.Format == TextureFormat::Depth32Float
                                        ? VK_IMAGE_ASPECT_DEPTH_BIT
                                        : VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = t.Desc.MipLevels;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.imageMemoryBarrierCount = 1;
    d.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(c, &d);
    t.Layout = next;
  }
  static VkRenderingAttachmentInfo Attachment(VkImageView view, LoadAction load,
                                              StoreAction store,
                                              const std::array<float, 4> &color,
                                              float depth, bool isDepth) {
    VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    a.imageView = view;
    a.imageLayout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a.loadOp =
        load == LoadAction::Load
            ? VK_ATTACHMENT_LOAD_OP_LOAD
            : (load == LoadAction::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                         : VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    a.storeOp = store == StoreAction::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                            : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    if (isDepth)
      a.clearValue.depthStencil = {depth, 0};
    else
      std::copy(color.begin(), color.end(), a.clearValue.color.float32);
    return a;
  }

  VkDescriptorSet DescriptorSet(PipelineBase *p, uint32_t slot, uint32_t set) {
    if (!p)
      throw std::runtime_error("Vulkan: bind pipeline before resources");
    if (set >= p->Descriptors.Sets[slot].size())
      throw std::runtime_error(
          "Vulkan: descriptor set outside pipeline layout");
    return p->Descriptors.Sets[slot][set];
  }
  VkDescriptorType Expected(PipelineBase *p, uint32_t set, uint32_t binding) {
    if (!p)
      throw std::runtime_error("Vulkan: no active pipeline");
    auto i = p->Descriptors.Types.find(BindingKey(set, binding));
    if (i == p->Descriptors.Types.end())
      throw std::runtime_error(
          "Vulkan: binding not reflected by active pipeline");
    return i->second;
  }
  void UpdateBufferDescriptor(PipelineBase *p, uint32_t slot, const Command &c,
                              VkDescriptorType type) {
    if (Expected(p, static_cast<uint32_t>(c.A), static_cast<uint32_t>(c.B)) !=
        type)
      throw std::runtime_error("Vulkan: buffer descriptor type mismatch");
    auto &b = Find(m_buffers, c.C, "buffer");
    VkDescriptorBufferInfo bi{b.Native.Handle, c.D,
                              c.E ? c.E : b.Desc.Size - c.D};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = DescriptorSet(p, slot, static_cast<uint32_t>(c.A));
    w.dstBinding = static_cast<uint32_t>(c.B);
    w.descriptorCount = 1;
    w.descriptorType = type;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
  }
  void UpdateTextureDescriptor(VkCommandBuffer cmd, PipelineBase *p,
                               uint32_t slot, const Command &c, bool storage) {
    auto &type = Find(m_textures, c.C, "texture");
    const VkDescriptorType expected =
        Expected(p, static_cast<uint32_t>(c.A), static_cast<uint32_t>(c.B));
    const VkDescriptorType wanted =
        storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : expected;
    if (storage && expected != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
      throw std::runtime_error("Vulkan: storage texture descriptor mismatch");
    Transition(cmd, type,
               storage ? VK_IMAGE_LAYOUT_GENERAL
                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkDescriptorImageInfo ii{};
    ii.imageView = type.Native.View;
    ii.imageLayout = type.Layout;
    if (!storage && c.D)
      ii.sampler = Find(m_samplers, c.D, "sampler").Native;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = DescriptorSet(p, slot, static_cast<uint32_t>(c.A));
    w.dstBinding = static_cast<uint32_t>(c.B);
    w.descriptorCount = 1;
    w.descriptorType = wanted;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
  }
  void UpdateSamplerDescriptor(PipelineBase *p, uint32_t slot,
                               const Command &c) {
    if (Expected(p, static_cast<uint32_t>(c.A), static_cast<uint32_t>(c.B)) !=
        VK_DESCRIPTOR_TYPE_SAMPLER)
      throw std::runtime_error("Vulkan: sampler descriptor mismatch");
    VkDescriptorImageInfo ii{};
    ii.sampler = Find(m_samplers, c.C, "sampler").Native;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = DescriptorSet(p, slot, static_cast<uint32_t>(c.A));
    w.dstBinding = static_cast<uint32_t>(c.B);
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
  }
  void BindDescriptorSets(VkCommandBuffer c, PipelineBase &p, uint32_t slot,
                          VkPipelineBindPoint point) {
    auto &sets = p.Descriptors.Sets[slot];
    if (!sets.empty())
      vkCmdBindDescriptorSets(c, point, p.Layout, 0,
                              static_cast<uint32_t>(sets.size()), sets.data(),
                              0, nullptr);
  }

  VkPhysicalDevice m_physical = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  VkQueue m_queue = VK_NULL_HANDLE;
  uint32_t m_queueFamily = 0;
  VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
  VkCommandPool m_uploadPool = VK_NULL_HANDLE;
  engine::vulkan::ResourceAllocator m_allocator;
  uint64_t m_nextHandle = 1;
  std::unordered_map<uint64_t, Shader> m_shaders;
  std::unordered_map<std::string, uint64_t> m_permutations;
  std::unordered_map<uint64_t, GraphicsPipeline> m_graphics;
  std::unordered_map<uint64_t, ComputePipeline> m_compute;
  std::unordered_map<uint64_t, Buffer> m_buffers;
  std::unordered_map<uint64_t, Texture> m_textures;
  std::unordered_map<uint64_t, Sampler> m_samplers;
  std::unordered_map<uint64_t, RenderTarget> m_targets;
  std::vector<Command> m_commands;
  std::string m_lastError;
};

} // namespace

std::unique_ptr<IGraphicsBackend>
CreateVulkanGraphicsBackend(const engine::NativeGraphicsContext &context) {
  return std::make_unique<VulkanGraphicsBackend>(context);
}

} // namespace rendering::detail
