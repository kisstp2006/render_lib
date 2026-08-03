#pragma once

#include "engine/renderer/Export.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace rendering {

class GraphicsDevice;

inline constexpr uint32_t ApiVersion = 4;

enum class Backend : uint32_t
{
    OpenGL = 0,
    Vulkan = 1,
};

struct ExternalWindowDesc
{
    void* UserData = nullptr;
    int32_t (*ShouldClose)(void*) = nullptr;
    void (*RequestClose)(void*) = nullptr;
    void (*PollEvents)(void*) = nullptr;
    int32_t (*MakeContextCurrent)(void*) = nullptr;
    void* (*GetGlProcAddress)(void*, const char*) = nullptr;
    void (*SwapBuffers)(void*) = nullptr;
    void (*SetSwapInterval)(void*, int32_t) = nullptr;
    const char* const* (*GetVulkanInstanceExtensions)(void*, uint32_t*) = nullptr;
    int32_t (*CreateVulkanSurface)(void*, void* instance,
                                   const void* allocator,
                                   uint64_t* surface) = nullptr;
};

struct RendererDesc
{
    Backend GraphicsBackend = Backend::OpenGL;
    std::string WindowTitle = "Rendering Engine SDK";
    uint32_t Width = 1280;
    uint32_t Height = 720;
    bool Resizable = true;
    bool Visible = true;
    bool VSync = true;
    bool Validation = false;
    uint32_t MsaaSamples = 4;
    std::filesystem::path ShaderDirectory;
    std::filesystem::path PipelineCacheDirectory;
    // Null keeps the standalone, renderer-owned GLFW window. A non-null host
    // lets an editor or managed application own presentation and its event
    // loop while the renderer owns only GPU objects.
    const ExternalWindowDesc* ExternalWindow = nullptr;
};

struct Vertex
{
    float Position[3]{};
    float Normal[3]{0.0f, 1.0f, 0.0f};
    float Tangent[4]{1.0f, 0.0f, 0.0f, 1.0f};
    float Uv[2]{};
};

struct MaterialDesc
{
    float Albedo[3]{0.8f, 0.8f, 0.8f};
    float Alpha = 1.0f;
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    float Emissive[3]{};
    float AmbientOcclusion = 1.0f;
    float SpecularF0 = 0.04f;
};

struct CameraDesc
{
    float Position[3]{0.0f, 1.8f, 6.0f};
    float YawDegrees = -90.0f;
    float PitchDegrees = -10.0f;
    float VerticalFovDegrees = 60.0f;
    float NearPlane = 0.05f;
    float FarPlane = 500.0f;
};

struct DirectionalLightDesc
{
    float Direction[3]{-0.4f, -0.85f, -0.35f};
    float Color[3]{1.0f, 0.96f, 0.88f};
    float Intensity = 3.0f;
    bool CastsShadows = true;
};

struct PointLightDesc
{
    float Position[3]{};
    float Color[3]{1.0f, 1.0f, 1.0f};
    float Intensity = 20.0f;
    float Radius = 15.0f;
    bool CastsShadows = false;
};

enum class AntiAliasingMode : int32_t
{
    None = 0,
    Fxaa = 1,
    Taa = 2,
};

struct PostProcessSettings
{
    bool Enabled = true;
    float Exposure = 2.2f;
    float BloomStrength = 0.12f;
    float BloomThreshold = 1.0f;
    float ShoulderStrength = 0.15f;
    float LinearStrength = 0.50f;
    float LinearAngle = 0.10f;
    float ToeStrength = 0.20f;
    float ToeNumerator = 0.02f;
    float ToeDenominator = 0.30f;
    float WhitePoint = 8.0f;
    bool AutoExposure = false;
    float AutoExposureKey = 0.18f;
    float AutoExposureMin = 0.4f;
    float AutoExposureMax = 3.0f;
    float AutoExposureSpeed = 1.8f;
    float Saturation = 1.0f;
    float Contrast = 1.0f;
    std::array<float, 3> ColorTint{1.0f, 1.0f, 1.0f};
    float ColorLutWeight = 0.0f;
    AntiAliasingMode AntiAliasing = AntiAliasingMode::Taa;
    float FxaaSubpixel = 0.75f;
    float FxaaEdgeThreshold = 0.125f;
    float FxaaEdgeThresholdMin = 0.0312f;
    float TaaHistoryWeight = 0.95f;
    float TaaSharpen = 0.06f;
    float TaaJitterScale = 0.5f;
    float TaaDepthThreshold = 0.0025f;
    bool LogPerformance = false;
};

struct FrameStats
{
    float GpuFrameMilliseconds = 0.0f;
    uint32_t DrawBatchCount = 0;
    uint32_t InstanceCount = 0;
    uint32_t DrawCallsSaved = 0;
};

using MeshHandle = uint64_t;
using MaterialHandle = uint64_t;
using ObjectHandle = uint64_t;
using PointLightHandle = uint64_t;

class Renderer;

struct ENGINE_RENDERER_API RendererDeleter
{
    void operator()(Renderer* renderer) const noexcept;
};

using RendererPtr = std::unique_ptr<Renderer, RendererDeleter>;

class ENGINE_RENDERER_API Renderer
{
public:
    // The custom pointer keeps allocation and destruction in the renderer
    // shared library, including MSVC Debug builds with separate CRT heaps.
    static RendererPtr Create(const RendererDesc& desc);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;

    // Processes host-window events. False means the window requested close.
    bool PumpEvents();
    void RequestClose();
    bool ShouldClose() const;
    void Resize(uint32_t width, uint32_t height);

    // RenderFrame performs no hidden simulation. Tick is the convenience
    // combination used by standalone samples: PumpEvents + RenderFrame.
    void RenderFrame(float deltaSeconds = 1.0f / 60.0f);
    bool Tick(float deltaSeconds = 1.0f / 60.0f);

    MeshHandle CreateMesh(std::span<const Vertex> vertices,
                          std::span<const uint32_t> indices);
    MeshHandle CreateCube(float halfExtent = 1.0f);
    MeshHandle CreateSphere(float radius = 1.0f, uint32_t stacks = 32,
                            uint32_t slices = 32);
    void DestroyMesh(MeshHandle mesh);

    MaterialHandle CreateMaterial(const MaterialDesc& desc = {});
    bool UpdateMaterial(MaterialHandle material, const MaterialDesc& desc);
    void DestroyMaterial(MaterialHandle material);

    ObjectHandle AddObject(MeshHandle mesh, MaterialHandle material,
                           const std::array<float, 16>& columnMajorTransform);
    bool SetObjectTransform(ObjectHandle object,
                            const std::array<float, 16>& columnMajorTransform);
    bool RemoveObject(ObjectHandle object);
    void ClearObjects();

    PointLightHandle AddPointLight(const PointLightDesc& desc);
    bool SetPointLight(PointLightHandle light, const PointLightDesc& desc);
    bool RemovePointLight(PointLightHandle light);
    void ClearPointLights();

    void SetCamera(const CameraDesc& desc);
    void SetSun(const DirectionalLightDesc& desc);
    void SetExposure(float exposure);
    void SetPostProcessSettings(const PostProcessSettings& settings);
    PostProcessSettings GetPostProcessSettings() const;
    void SetBackgroundColor(const std::array<float, 3>& zenith,
                            const std::array<float, 3>& horizon);
    void RequestScreenshot(const std::filesystem::path& path);

    FrameStats GetFrameStats() const;
    std::string_view BackendName() const;
    Backend ActiveBackend() const;
    GraphicsDevice& GetGraphicsDevice();
    const GraphicsDevice& GetGraphicsDevice() const;

private:
    friend struct RendererDeleter;
    struct Impl;
    explicit Renderer(Impl* impl);
    Impl* m_impl = nullptr;
};

ENGINE_RENDERER_API std::array<float, 16> IdentityTransform();
ENGINE_RENDERER_API std::array<float, 16> ComposeTransform(
    const std::array<float, 3>& translation,
    const std::array<float, 3>& rotationDegrees,
    const std::array<float, 3>& scale);

} // namespace rendering
