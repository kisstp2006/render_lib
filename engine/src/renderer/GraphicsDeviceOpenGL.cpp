#include "GraphicsDeviceInternal.h"
#include "ShaderCompilerCommon.h"

#include <glad/gl.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rendering::detail {
namespace {

GLenum GlStage(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return GL_VERTEX_SHADER;
  case ShaderStage::Fragment:
    return GL_FRAGMENT_SHADER;
  case ShaderStage::Compute:
    return GL_COMPUTE_SHADER;
  }
  throw std::runtime_error("OpenGL: invalid shader stage");
}

GLenum GlTopology(PrimitiveTopology topology) {
  switch (topology) {
  case PrimitiveTopology::TriangleList:
    return GL_TRIANGLES;
  case PrimitiveTopology::TriangleStrip:
    return GL_TRIANGLE_STRIP;
  case PrimitiveTopology::LineList:
    return GL_LINES;
  case PrimitiveTopology::PointList:
    return GL_POINTS;
  }
  return GL_TRIANGLES;
}

GLenum GlCompare(CompareOperation op) {
  constexpr GLenum values[] = {GL_NEVER,  GL_LESS,    GL_LEQUAL, GL_EQUAL,
                               GL_GEQUAL, GL_GREATER, GL_ALWAYS};
  return values[static_cast<size_t>(op)];
}

struct GlTextureFormat {
  GLenum Internal = GL_RGBA8;
  GLenum Format = GL_RGBA;
  GLenum Type = GL_UNSIGNED_BYTE;
  bool Depth = false;
};

GlTextureFormat GlFormat(TextureFormat format) {
  switch (format) {
  case TextureFormat::R8Unorm:
    return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, false};
  case TextureFormat::RG8Unorm:
    return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, false};
  case TextureFormat::RGBA8Unorm:
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false};
  case TextureFormat::RGBA8Srgb:
    return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, false};
  case TextureFormat::RGBA16Float:
    return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, false};
  case TextureFormat::R32Float:
    return {GL_R32F, GL_RED, GL_FLOAT, false};
  case TextureFormat::Depth32Float:
    return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, true};
  }
  throw std::runtime_error("OpenGL: unsupported texture format");
}

struct AttributeFormat {
  GLint Count;
  GLenum Type;
  GLboolean Normalized;
  bool Integer;
};

AttributeFormat GlVertexFormat(VertexFormat format) {
  switch (format) {
  case VertexFormat::Float:
    return {1, GL_FLOAT, GL_FALSE, false};
  case VertexFormat::Float2:
    return {2, GL_FLOAT, GL_FALSE, false};
  case VertexFormat::Float3:
    return {3, GL_FLOAT, GL_FALSE, false};
  case VertexFormat::Float4:
    return {4, GL_FLOAT, GL_FALSE, false};
  case VertexFormat::UInt:
    return {1, GL_UNSIGNED_INT, GL_FALSE, true};
  case VertexFormat::UInt2:
    return {2, GL_UNSIGNED_INT, GL_FALSE, true};
  case VertexFormat::UInt4:
    return {4, GL_UNSIGNED_INT, GL_FALSE, true};
  case VertexFormat::UByte4Normalized:
    return {4, GL_UNSIGNED_BYTE, GL_TRUE, false};
  }
  throw std::runtime_error("OpenGL: unsupported vertex format");
}

GLuint CreateNativeShader(const CompiledShader &compiled) {
  const GLuint shader = glCreateShader(GlStage(compiled.Desc.Stage));
  glShaderBinary(
      1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V, compiled.Spirv.data(),
      static_cast<GLsizei>(compiled.Spirv.size() * sizeof(uint32_t)));
  glSpecializeShader(shader, compiled.Desc.EntryPoint.c_str(), 0, nullptr,
                     nullptr);
  GLint success = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (success != GL_TRUE) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("OpenGL shader specialization failed: " + log);
  }
  return shader;
}

GLuint LinkProgram(std::span<const GLuint> shaders) {
  const GLuint program = glCreateProgram();
  for (GLuint shader : shaders)
    glAttachShader(program, shader);
  glLinkProgram(program);
  GLint success = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (success != GL_TRUE) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error("OpenGL program link failed: " + log);
  }
  return program;
}

template <class Map> auto &Find(Map &map, uint64_t handle, const char *type) {
  const auto it = map.find(handle);
  if (it == map.end())
    throw std::runtime_error(std::string("OpenGL: invalid ") + type +
                             " handle");
  return it->second;
}

class OpenGlGraphicsBackend final : public IGraphicsBackend {
  struct Shader {
    CompiledShader Compiled;
    GLuint Native = 0;
  };
  struct GraphicsPipeline {
    GraphicsPipelineDesc Desc;
    GLuint Program = 0;
    GLuint Vao = 0;
  };
  struct ComputePipeline {
    ComputePipelineDesc Desc;
    GLuint Program = 0;
  };
  struct Buffer {
    BufferDesc Desc;
    GLuint Native = 0;
  };
  struct Texture {
    TextureDesc Desc;
    GLuint Native = 0;
  };
  struct Sampler {
    SamplerDesc Desc;
    GLuint Native = 0;
  };
  struct RenderTarget {
    RenderTargetDesc Desc;
    GLuint Fbo = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
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
  ~OpenGlGraphicsBackend() override {
    for (auto &[_, p] : m_graphics) {
      glDeleteVertexArrays(1, &p.Vao);
      glDeleteProgram(p.Program);
    }
    for (auto &[_, p] : m_compute)
      glDeleteProgram(p.Program);
    for (auto &[_, s] : m_shaders)
      glDeleteShader(s.Native);
    for (auto &[_, b] : m_buffers)
      glDeleteBuffers(1, &b.Native);
    for (auto &[_, t] : m_textures)
      glDeleteTextures(1, &t.Native);
    for (auto &[_, s] : m_samplers)
      glDeleteSamplers(1, &s.Native);
    for (auto &[_, r] : m_targets)
      glDeleteFramebuffers(1, &r.Fbo);
  }

  ShaderModuleHandle CreateShader(const ShaderModuleDesc &desc) override {
    CompiledShader compiled =
        CompileShader(desc, engine::shader::SpirvTarget::OpenGL46);
    const GLuint native = CreateNativeShader(compiled);
    const uint64_t handle = Next();
    m_permutations[compiled.Reflection.PermutationKey] = handle;
    m_shaders.emplace(handle, Shader{std::move(compiled), native});
    return handle;
  }

  ShaderModuleHandle
  CreatePermutation(ShaderModuleHandle base,
                    std::span<const ShaderDefine> additional) override {
    ShaderModuleDesc desc = Find(m_shaders, base, "shader").Compiled.Desc;
    desc.Defines.insert(desc.Defines.end(), additional.begin(),
                        additional.end());
    CompiledShader compiled =
        CompileShader(desc, engine::shader::SpirvTarget::OpenGL46);
    if (const auto found =
            m_permutations.find(compiled.Reflection.PermutationKey);
        found != m_permutations.end())
      return found->second;
    const GLuint native = CreateNativeShader(compiled);
    const uint64_t handle = Next();
    m_permutations[compiled.Reflection.PermutationKey] = handle;
    m_shaders.emplace(handle, Shader{std::move(compiled), native});
    return handle;
  }

  void DestroyShader(ShaderModuleHandle handle) override {
    const auto found = m_shaders.find(handle);
    if (found == m_shaders.end())
      return;
    glDeleteShader(found->second.Native);
    m_permutations.erase(found->second.Compiled.Reflection.PermutationKey);
    m_shaders.erase(found);
  }
  const ShaderReflection &Reflection(ShaderModuleHandle h) const override {
    return Find(m_shaders, h, "shader").Compiled.Reflection;
  }

  GraphicsPipelineHandle
  CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override {
    if (!desc.VertexShader || !desc.FragmentShader)
      throw std::runtime_error(
          "OpenGL: graphics pipeline requires vertex and fragment shaders");
    const GLuint shaders[] = {
        Find(m_shaders, desc.VertexShader, "shader").Native,
        Find(m_shaders, desc.FragmentShader, "shader").Native};
    GraphicsPipeline pipeline;
    pipeline.Desc = desc;
    pipeline.Program = LinkProgram(shaders);
    glCreateVertexArrays(1, &pipeline.Vao);
    for (const VertexBindingDesc &binding : desc.VertexBindings)
      glVertexArrayBindingDivisor(
          pipeline.Vao, binding.Binding,
          binding.InputRate == VertexInputRate::PerInstance ? 1 : 0);
    for (const VertexAttributeDesc &attribute : desc.VertexAttributes) {
      const AttributeFormat format = GlVertexFormat(attribute.Format);
      glEnableVertexArrayAttrib(pipeline.Vao, attribute.Location);
      if (format.Integer)
        glVertexArrayAttribIFormat(pipeline.Vao, attribute.Location,
                                   format.Count, format.Type, attribute.Offset);
      else
        glVertexArrayAttribFormat(pipeline.Vao, attribute.Location,
                                  format.Count, format.Type, format.Normalized,
                                  attribute.Offset);
      glVertexArrayAttribBinding(pipeline.Vao, attribute.Location,
                                 attribute.Binding);
    }
    const uint64_t handle = Next();
    m_graphics.emplace(handle, std::move(pipeline));
    return handle;
  }

  ComputePipelineHandle
  CreateComputePipeline(const ComputePipelineDesc &desc) override {
    const GLuint shader = Find(m_shaders, desc.ComputeShader, "shader").Native;
    const uint64_t handle = Next();
    m_compute.emplace(handle, ComputePipeline{desc, LinkProgram({&shader, 1})});
    return handle;
  }
  void DestroyGraphicsPipeline(GraphicsPipelineHandle h) override {
    const auto it = m_graphics.find(h);
    if (it == m_graphics.end())
      return;
    glDeleteVertexArrays(1, &it->second.Vao);
    glDeleteProgram(it->second.Program);
    m_graphics.erase(it);
  }
  void DestroyComputePipeline(ComputePipelineHandle h) override {
    const auto it = m_compute.find(h);
    if (it == m_compute.end())
      return;
    glDeleteProgram(it->second.Program);
    m_compute.erase(it);
  }

  BufferHandle CreateBuffer(const BufferDesc &desc,
                            std::span<const std::byte> data) override {
    if (!desc.Size || data.size() > desc.Size)
      throw std::runtime_error("OpenGL: invalid buffer size");
    Buffer buffer{desc};
    glCreateBuffers(1, &buffer.Native);
    glNamedBufferStorage(buffer.Native, static_cast<GLsizeiptr>(desc.Size),
                         data.empty() ? nullptr : data.data(),
                         desc.CpuWritable ? GL_DYNAMIC_STORAGE_BIT : 0);
    const uint64_t handle = Next();
    m_buffers.emplace(handle, buffer);
    return handle;
  }
  bool UpdateBuffer(BufferHandle h, uint64_t offset,
                    std::span<const std::byte> data) override {
    Buffer &buffer = Find(m_buffers, h, "buffer");
    if (offset + data.size() > buffer.Desc.Size)
      return false;
    glNamedBufferSubData(buffer.Native, static_cast<GLintptr>(offset),
                         static_cast<GLsizeiptr>(data.size()), data.data());
    return true;
  }
  void DestroyBuffer(BufferHandle h) override {
    const auto it = m_buffers.find(h);
    if (it != m_buffers.end()) {
      glDeleteBuffers(1, &it->second.Native);
      m_buffers.erase(it);
    }
  }

  TextureHandle CreateTexture(const TextureDesc &desc,
                              std::span<const std::byte> data) override {
    if (!desc.Width || !desc.Height || !desc.MipLevels)
      throw std::runtime_error("OpenGL: invalid texture size");
    Texture texture{desc};
    const GlTextureFormat format = GlFormat(desc.Format);
    glCreateTextures(GL_TEXTURE_2D, 1, &texture.Native);
    glTextureStorage2D(texture.Native, static_cast<GLsizei>(desc.MipLevels),
                       format.Internal, static_cast<GLsizei>(desc.Width),
                       static_cast<GLsizei>(desc.Height));
    glTextureParameteri(texture.Native, GL_TEXTURE_MIN_FILTER,
                        desc.MipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR
                                           : GL_LINEAR);
    glTextureParameteri(texture.Native, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture.Native, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(texture.Native, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (!data.empty())
      glTextureSubImage2D(texture.Native, 0, 0, 0, desc.Width, desc.Height,
                          format.Format, format.Type, data.data());
    const uint64_t handle = Next();
    m_textures.emplace(handle, texture);
    return handle;
  }
  bool UpdateTexture(TextureHandle h, uint32_t mip,
                     std::span<const std::byte> data) override {
    Texture &texture = Find(m_textures, h, "texture");
    if (mip >= texture.Desc.MipLevels)
      return false;
    const auto format = GlFormat(texture.Desc.Format);
    const uint32_t w = std::max(1u, texture.Desc.Width >> mip);
    const uint32_t he = std::max(1u, texture.Desc.Height >> mip);
    glTextureSubImage2D(texture.Native, mip, 0, 0, w, he, format.Format,
                        format.Type, data.data());
    return true;
  }
  void DestroyTexture(TextureHandle h) override {
    const auto it = m_textures.find(h);
    if (it != m_textures.end()) {
      glDeleteTextures(1, &it->second.Native);
      m_textures.erase(it);
    }
  }

  SamplerHandle CreateSampler(const SamplerDesc &desc) override {
    Sampler sampler{desc};
    glCreateSamplers(1, &sampler.Native);
    glSamplerParameteri(sampler.Native, GL_TEXTURE_MIN_FILTER,
                        desc.MinFilter == Filter::Linear ? GL_LINEAR
                                                         : GL_NEAREST);
    glSamplerParameteri(sampler.Native, GL_TEXTURE_MAG_FILTER,
                        desc.MagFilter == Filter::Linear ? GL_LINEAR
                                                         : GL_NEAREST);
    const auto address = [](AddressMode mode) {
      switch (mode) {
      case AddressMode::Repeat:
        return GL_REPEAT;
      case AddressMode::MirroredRepeat:
        return GL_MIRRORED_REPEAT;
      case AddressMode::ClampToEdge:
        return GL_CLAMP_TO_EDGE;
      case AddressMode::ClampToBorder:
        return GL_CLAMP_TO_BORDER;
      }
      return GL_REPEAT;
    };
    glSamplerParameteri(sampler.Native, GL_TEXTURE_WRAP_S,
                        address(desc.AddressU));
    glSamplerParameteri(sampler.Native, GL_TEXTURE_WRAP_T,
                        address(desc.AddressV));
    glSamplerParameteri(sampler.Native, GL_TEXTURE_WRAP_R,
                        address(desc.AddressW));
    if (desc.MaxAnisotropy > 1.0f)
      glSamplerParameterf(sampler.Native, GL_TEXTURE_MAX_ANISOTROPY,
                          std::min(desc.MaxAnisotropy, 16.0f));
    const uint64_t handle = Next();
    m_samplers.emplace(handle, sampler);
    return handle;
  }
  void DestroySampler(SamplerHandle h) override {
    const auto it = m_samplers.find(h);
    if (it != m_samplers.end()) {
      glDeleteSamplers(1, &it->second.Native);
      m_samplers.erase(it);
    }
  }

  RenderTargetHandle CreateRenderTarget(const RenderTargetDesc &desc) override {
    if (desc.ColorAttachments.empty() && !desc.DepthAttachment)
      throw std::runtime_error("OpenGL: empty render target");
    RenderTarget target;
    target.Desc = desc;
    glCreateFramebuffers(1, &target.Fbo);
    std::vector<GLenum> draws;
    for (size_t i = 0; i < desc.ColorAttachments.size(); ++i) {
      auto &tex = Find(m_textures, desc.ColorAttachments[i], "texture");
      if (i == 0) {
        target.Width = tex.Desc.Width;
        target.Height = tex.Desc.Height;
      } else if (tex.Desc.Width != target.Width ||
                 tex.Desc.Height != target.Height)
        throw std::runtime_error("OpenGL: render target size mismatch");
      glNamedFramebufferTexture(target.Fbo,
                                GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                                tex.Native, 0);
      draws.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
    }
    if (desc.DepthAttachment) {
      auto &tex = Find(m_textures, desc.DepthAttachment, "texture");
      if (!target.Width) {
        target.Width = tex.Desc.Width;
        target.Height = tex.Desc.Height;
      }
      if (tex.Desc.Width != target.Width || tex.Desc.Height != target.Height)
        throw std::runtime_error("OpenGL: depth size mismatch");
      glNamedFramebufferTexture(target.Fbo, GL_DEPTH_ATTACHMENT, tex.Native, 0);
    }
    if (draws.empty())
      glNamedFramebufferDrawBuffer(target.Fbo, GL_NONE);
    else
      glNamedFramebufferDrawBuffers(
          target.Fbo, static_cast<GLsizei>(draws.size()), draws.data());
    if (glCheckNamedFramebufferStatus(target.Fbo, GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
      glDeleteFramebuffers(1, &target.Fbo);
      throw std::runtime_error("OpenGL: incomplete render target");
    }
    const uint64_t handle = Next();
    m_targets.emplace(handle, std::move(target));
    return handle;
  }
  void DestroyRenderTarget(RenderTargetHandle h) override {
    const auto it = m_targets.find(h);
    if (it != m_targets.end()) {
      glDeleteFramebuffers(1, &it->second.Fbo);
      m_targets.erase(it);
    }
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
    uint32_t count = 0;
    m_lastError.clear();
    for (auto &[handle, shader] : m_shaders) {
      if (!ShaderFilesChanged(shader.Compiled))
        continue;
      try {
        CompiledShader replacement = CompileShader(
            shader.Compiled.Desc, engine::shader::SpirvTarget::OpenGL46);
        GLuint native = CreateNativeShader(replacement);
        std::vector<std::pair<GraphicsPipelineHandle, GLuint>> graphicsPrograms;
        std::vector<std::pair<ComputePipelineHandle, GLuint>> computePrograms;
        try {
          for (auto &[id, p] : m_graphics)
            if (p.Desc.VertexShader == handle ||
                p.Desc.FragmentShader == handle) {
              GLuint modules[] = {
                  p.Desc.VertexShader == handle
                      ? native
                      : Find(m_shaders, p.Desc.VertexShader, "shader").Native,
                  p.Desc.FragmentShader == handle
                      ? native
                      : Find(m_shaders, p.Desc.FragmentShader, "shader")
                            .Native};
              graphicsPrograms.emplace_back(id, LinkProgram(modules));
            }
          for (auto &[id, p] : m_compute)
            if (p.Desc.ComputeShader == handle) {
              computePrograms.emplace_back(id, LinkProgram({&native, 1}));
            }
        } catch (...) {
          for (auto [_, p] : graphicsPrograms)
            glDeleteProgram(p);
          for (auto [_, p] : computePrograms)
            glDeleteProgram(p);
          glDeleteShader(native);
          throw;
        }
        glDeleteShader(shader.Native);
        shader.Native = native;
        replacement.Reflection.Generation =
            shader.Compiled.Reflection.Generation + 1;
        shader.Compiled = std::move(replacement);
        for (auto [id, p] : graphicsPrograms) {
          glDeleteProgram(m_graphics[id].Program);
          m_graphics[id].Program = p;
        }
        for (auto [id, p] : computePrograms) {
          glDeleteProgram(m_compute[id].Program);
          m_compute[id].Program = p;
        }
        ++count;
      } catch (const std::exception &e) {
        m_lastError = e.what();
      }
    }
    return count;
  }
  std::string_view LastShaderError() const override { return m_lastError; }

  void Execute(const engine::NativeCustomRenderContext &frame) override {
    GraphicsPipeline *graphics = nullptr;
    ComputePipeline *compute = nullptr;
    IndexType indexType = IndexType::UInt32;
    uint64_t indexOffset = 0;
    for (const Command &c : m_commands) {
      switch (c.Type) {
      case Op::BeginPass: {
        GLuint fbo = static_cast<GLuint>(frame.ColorTarget);
        uint32_t w = frame.Width, h = frame.Height;
        if (c.Pass.Target) {
          auto &target = Find(m_targets, c.Pass.Target, "render target");
          fbo = target.Fbo;
          w = target.Width;
          h = target.Height;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        GLbitfield clear = 0;
        if (c.Pass.ColorLoad == LoadAction::Clear) {
          glClearColor(c.Pass.ClearColor[0], c.Pass.ClearColor[1],
                       c.Pass.ClearColor[2], c.Pass.ClearColor[3]);
          clear |= GL_COLOR_BUFFER_BIT;
        }
        if (c.Pass.DepthLoad == LoadAction::Clear) {
          glClearDepth(c.Pass.ClearDepth);
          clear |= GL_DEPTH_BUFFER_BIT;
        }
        if (clear)
          glClear(clear);
        break;
      }
      case Op::EndPass:
        break;
      case Op::BindGraphics: {
        graphics = &Find(m_graphics, c.A, "graphics pipeline");
        compute = nullptr;
        glUseProgram(graphics->Program);
        glBindVertexArray(graphics->Vao);
        ApplyState(graphics->Desc);
        break;
      }
      case Op::BindCompute: {
        compute = &Find(m_compute, c.A, "compute pipeline");
        graphics = nullptr;
        glUseProgram(compute->Program);
        break;
      }
      case Op::VertexBuffer: {
        if (!graphics)
          throw std::runtime_error(
              "OpenGL: bind graphics pipeline before vertex buffer");
        auto &b = Find(m_buffers, c.B, "buffer");
        const auto it =
            std::find_if(graphics->Desc.VertexBindings.begin(),
                         graphics->Desc.VertexBindings.end(),
                         [&](const auto &x) { return x.Binding == c.A; });
        if (it == graphics->Desc.VertexBindings.end())
          throw std::runtime_error(
              "OpenGL: missing vertex binding description");
        glVertexArrayVertexBuffer(graphics->Vao, static_cast<GLuint>(c.A),
                                  b.Native, static_cast<GLintptr>(c.C),
                                  static_cast<GLsizei>(it->Stride));
        break;
      }
      case Op::IndexBuffer: {
        if (!graphics)
          throw std::runtime_error(
              "OpenGL: bind graphics pipeline before index buffer");
        auto &b = Find(m_buffers, c.A, "buffer");
        glVertexArrayElementBuffer(graphics->Vao, b.Native);
        indexType = static_cast<IndexType>(c.B);
        indexOffset = c.C;
        break;
      }
      case Op::UniformBuffer:
        BindBufferRange(GL_UNIFORM_BUFFER, c);
        break;
      case Op::StorageBuffer:
        BindBufferRange(GL_SHADER_STORAGE_BUFFER, c);
        break;
      case Op::Texture: {
        auto &t = Find(m_textures, c.C, "texture");
        glBindTextureUnit(static_cast<GLuint>(c.B), t.Native);
        glBindSampler(static_cast<GLuint>(c.B),
                      c.D ? Find(m_samplers, c.D, "sampler").Native : 0);
        break;
      }
      case Op::SamplerBinding:
        glBindSampler(static_cast<GLuint>(c.B),
                      Find(m_samplers, c.C, "sampler").Native);
        break;
      case Op::StorageTexture: {
        auto &t = Find(m_textures, c.C, "texture");
        const auto f = GlFormat(t.Desc.Format);
        glBindImageTexture(static_cast<GLuint>(c.B), t.Native, 0, GL_FALSE, 0,
                           GL_READ_WRITE, f.Internal);
        break;
      }
      case Op::Draw:
        if (!graphics)
          throw std::runtime_error("OpenGL: no graphics pipeline");
        glDrawArraysInstancedBaseInstance(
            GlTopology(graphics->Desc.Topology), static_cast<GLint>(c.C),
            static_cast<GLsizei>(c.A), static_cast<GLsizei>(c.B),
            static_cast<GLuint>(c.D));
        break;
      case Op::DrawIndexed:
        if (!graphics)
          throw std::runtime_error("OpenGL: no graphics pipeline");
        {
          const uint64_t stride = indexType == IndexType::UInt16 ? 2 : 4;
          const void *offset = reinterpret_cast<const void *>(
              static_cast<uintptr_t>(indexOffset + c.C * stride));
          glDrawElementsInstancedBaseVertexBaseInstance(
              GlTopology(graphics->Desc.Topology), static_cast<GLsizei>(c.A),
              indexType == IndexType::UInt16 ? GL_UNSIGNED_SHORT
                                             : GL_UNSIGNED_INT,
              offset, static_cast<GLsizei>(c.B), c.Signed,
              static_cast<GLuint>(c.D));
        }
        break;
      case Op::Dispatch:
        if (!compute)
          throw std::runtime_error("OpenGL: no compute pipeline");
        glDispatchCompute(static_cast<GLuint>(c.A), static_cast<GLuint>(c.B),
                          static_cast<GLuint>(c.C));
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        break;
      }
    }
    m_commands.clear();
  }

private:
  uint64_t Next() { return m_nextHandle++; }
  void Push(Op op, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0,
            uint64_t d = 0, uint64_t e = 0) {
    Command q;
    q.Type = op;
    q.A = a;
    q.B = b;
    q.C = c;
    q.D = d;
    q.E = e;
    m_commands.push_back(q);
  }
  void BindBufferRange(GLenum target, const Command &c) {
    auto &b = Find(m_buffers, c.C, "buffer");
    const uint64_t size = c.E ? c.E : b.Desc.Size - c.D;
    glBindBufferRange(target, static_cast<GLuint>(c.B), b.Native,
                      static_cast<GLintptr>(c.D),
                      static_cast<GLsizeiptr>(size));
  }
  static void ApplyState(const GraphicsPipelineDesc &d) {
    if (d.Cull == CullMode::None)
      glDisable(GL_CULL_FACE);
    else {
      glEnable(GL_CULL_FACE);
      glCullFace(d.Cull == CullMode::Back ? GL_BACK : GL_FRONT);
    }
    glFrontFace(d.Winding == FrontFace::CounterClockwise ? GL_CCW : GL_CW);
    if (d.DepthTest) {
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GlCompare(d.DepthCompare));
    } else
      glDisable(GL_DEPTH_TEST);
    glDepthMask(d.DepthWrite ? GL_TRUE : GL_FALSE);
    if (d.Blend.Enabled) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else
      glDisable(GL_BLEND);
  }

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

std::unique_ptr<IGraphicsBackend> CreateOpenGlGraphicsBackend() {
  return std::make_unique<OpenGlGraphicsBackend>();
}

} // namespace rendering::detail
