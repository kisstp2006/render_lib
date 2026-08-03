#include <engine/renderer/GraphicsDevice.h>
#include <engine/renderer/Renderer.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

namespace {
struct CustomVertex {
  float Position[2];
  float Uv[2];
};
struct alignas(16) TintUniform {
  float Color[4];
};

template <class T> std::span<const std::byte> Bytes(const T &value) {
  return std::as_bytes(std::span(&value, 1));
}

template <class T, size_t N>
std::span<const std::byte> Bytes(const std::array<T, N> &value) {
  return std::as_bytes(std::span(value));
}
} // namespace

int main(int argc, char **argv) try {
  rendering::RendererDesc rendererDesc;
  rendererDesc.WindowTitle = "Custom GraphicsDevice";
  rendererDesc.Width = 960;
  rendererDesc.Height = 540;
  rendererDesc.Validation = true;
  int frames = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--vulkan")
      rendererDesc.GraphicsBackend = rendering::Backend::Vulkan;
    else if (arg == "--hidden")
      rendererDesc.Visible = false;
    else if (arg.starts_with("--frames="))
      frames = std::stoi(std::string(arg.substr(9)));
  }
  auto renderer = rendering::Renderer::Create(rendererDesc);
  auto &gpu = renderer->GetGraphicsDevice();
  const std::filesystem::path shaderRoot = CUSTOM_SHADER_DIR;

  rendering::ShaderModuleDesc vertexDesc;
  vertexDesc.Stage = rendering::ShaderStage::Vertex;
  vertexDesc.SourcePath = shaderRoot / "custom.hlsl";
  vertexDesc.EntryPoint = "VSMain";
  const auto vertexShader = gpu.CreateShaderModule(vertexDesc);
  rendering::ShaderModuleDesc fragmentDesc = vertexDesc;
  fragmentDesc.Stage = rendering::ShaderStage::Fragment;
  fragmentDesc.EntryPoint = "PSMain";
  const auto fragmentShader = gpu.CreateShaderModule(fragmentDesc);
  const std::array fragmentDefines{
      rendering::ShaderDefine{"CUSTOM_VARIANT", "1"}};
  const auto fragmentPermutation =
      gpu.CreateShaderPermutation(fragmentShader, fragmentDefines);
  rendering::ShaderModuleDesc glslDesc;
  glslDesc.Stage = rendering::ShaderStage::Vertex;
  glslDesc.Language = rendering::ShaderLanguage::Glsl;
  glslDesc.SourcePath = "inline_custom.vert.glsl";
  glslDesc.Source = R"(#version 450
layout(location = 0) in vec2 Position;
void main() { gl_Position = vec4(Position, 0.0, 1.0); })";
  glslDesc.EnableHotReload = false;
  const auto glslShader = gpu.CreateShaderModule(glslDesc);
  rendering::ShaderModuleDesc computeDesc;
  computeDesc.Stage = rendering::ShaderStage::Compute;
  computeDesc.SourcePath = shaderRoot / "compute.hlsl";
  computeDesc.EntryPoint = "CSMain";
  const auto computeShader = gpu.CreateShaderModule(computeDesc);

  rendering::GraphicsPipelineDesc onscreenDesc;
  onscreenDesc.VertexShader = vertexShader;
  onscreenDesc.FragmentShader = fragmentPermutation;
  onscreenDesc.Cull = rendering::CullMode::None;
  onscreenDesc.VertexBindings = {
      {0, sizeof(CustomVertex), rendering::VertexInputRate::PerVertex}};
  onscreenDesc.VertexAttributes = {
      {0, 0, rendering::VertexFormat::Float2, offsetof(CustomVertex, Position)},
      {1, 0, rendering::VertexFormat::Float2, offsetof(CustomVertex, Uv)}};
  const auto onscreenPipeline = gpu.CreateGraphicsPipeline(onscreenDesc);
  rendering::GraphicsPipelineDesc offscreenDesc = onscreenDesc;
  offscreenDesc.ColorFormats = {rendering::TextureFormat::RGBA8Unorm};
  const auto offscreenPipeline = gpu.CreateGraphicsPipeline(offscreenDesc);
  const auto computePipeline =
      gpu.CreateComputePipeline({computeShader, "Custom compute"});

  const std::array vertices{CustomVertex{{-0.75f, -0.70f}, {0.0f, 1.0f}},
                            CustomVertex{{0.75f, -0.70f}, {1.0f, 1.0f}},
                            CustomVertex{{0.00f, 0.75f}, {0.5f, 0.0f}}};
  rendering::BufferDesc vertexBufferDesc{sizeof(vertices),
                                         rendering::BufferUsage::Vertex};
  const auto vertexBuffer = gpu.CreateBuffer(vertexBufferDesc, Bytes(vertices));
  const TintUniform tint{{0.25f, 0.85f, 1.0f, 1.0f}};
  rendering::BufferDesc uniformDesc{sizeof(tint),
                                    rendering::BufferUsage::Uniform, true};
  const auto uniformBuffer = gpu.CreateBuffer(uniformDesc, Bytes(tint));
  std::array<uint32_t, 4> computeData{};
  rendering::BufferDesc storageDesc{sizeof(computeData),
                                    rendering::BufferUsage::Storage, true};
  const auto storageBuffer = gpu.CreateBuffer(storageDesc, Bytes(computeData));

  const std::array<uint8_t, 16> checker{255, 80, 35,  255, 35,  80, 255, 255,
                                        35,  80, 255, 255, 255, 80, 35,  255};
  rendering::TextureDesc checkerDesc{
      2,
      2,
      1,
      rendering::TextureFormat::RGBA8Unorm,
      rendering::TextureUsage::Sampled |
          rendering::TextureUsage::TransferDestination,
      "Checker"};
  const auto checkerTexture = gpu.CreateTexture(checkerDesc, Bytes(checker));
  const auto sampler = gpu.CreateSampler();
  rendering::TextureDesc targetTextureDesc{
      960,
      540,
      1,
      rendering::TextureFormat::RGBA8Unorm,
      rendering::TextureUsage::ColorAttachment |
          rendering::TextureUsage::Sampled,
      "Offscreen color"};
  const auto targetTexture = gpu.CreateTexture(targetTextureDesc);
  const auto target =
      gpu.CreateRenderTarget({{targetTexture}, 0, "Offscreen target"});

  std::cout << "Vertex reflection resources: "
            << gpu.GetShaderReflection(vertexShader).Resources.size()
            << ", GLSL inputs: "
            << gpu.GetShaderReflection(glslShader).Resources.size() << '\n';
  int rendered = 0;
  while (!renderer->ShouldClose() && (frames == 0 || rendered < frames)) {
    renderer->PumpEvents();
    gpu.ReloadChangedShaders();
    gpu.BindComputePipeline(computePipeline);
    gpu.BindStorageBuffer(0, 0, storageBuffer);
    gpu.Dispatch(4);

    rendering::RenderPassDesc offscreenPass;
    offscreenPass.Target = target;
    offscreenPass.ColorLoad = rendering::LoadAction::Clear;
    offscreenPass.ClearColor = {0.02f, 0.025f, 0.04f, 1.0f};
    gpu.BeginRenderPass(offscreenPass);
    gpu.BindGraphicsPipeline(offscreenPipeline);
    gpu.BindVertexBuffer(0, vertexBuffer);
    gpu.BindUniformBuffer(0, 0, uniformBuffer);
    gpu.BindTexture(0, 1, checkerTexture, sampler);
    gpu.BindSampler(0, 2, sampler);
    gpu.Draw(3);
    gpu.EndRenderPass();

    rendering::RenderPassDesc presentPass;
    presentPass.ColorLoad = rendering::LoadAction::Load;
    gpu.BeginRenderPass(presentPass);
    gpu.BindGraphicsPipeline(onscreenPipeline);
    gpu.BindVertexBuffer(0, vertexBuffer);
    gpu.BindUniformBuffer(0, 0, uniformBuffer);
    gpu.BindTexture(0, 1, targetTexture, sampler);
    gpu.BindSampler(0, 2, sampler);
    gpu.Draw(3);
    gpu.EndRenderPass();
    renderer->RenderFrame();
    ++rendered;
  }
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
