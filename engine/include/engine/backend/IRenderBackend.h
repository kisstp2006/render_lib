#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <string>

#include "engine/debug/FrameDebugger.h"
#include "engine/render/GpuCapabilities.h"
#include "engine/render/PipelineCache.h"
#include "engine/render/RenderGraph.h"

namespace engine
{

class Window;
struct RenderFrameData;

// Presentation is a shared application-level choice. Backends translate it
// to the closest native mode (swap interval for OpenGL, present mode for
// Vulkan) and report the modes actually supported by the selected device.
enum class PresentMode
{
    Immediate,
    VSync,
    Adaptive
};

struct RenderBackendConfig
{
    PresentMode Presentation = PresentMode::VSync;
    uint32_t MsaaSamples = 4;
    float MaxAnisotropy = 8.0f;
    bool PreferDiscreteGpu = true;
    std::string PreferredAdapter;
#ifdef NDEBUG
    bool EnableValidation = false;
#else
    bool EnableValidation = true;
#endif
    bool EnableGpuTiming = true;
    GpuCapabilityPolicy CapabilityPolicy = GpuCapabilityPolicy::Default;
    bool EnableDriverWorkarounds = true;
    bool EnablePipelineCache = true;
    bool ClearPipelineCache = false;
    // Empty selects the build/runtime default. A fixed engine-owned subtree
    // and device hash are always appended before any files are touched.
    std::string PipelineCacheDirectory;
    bool EnableRenderGraph = true;
    bool ValidateRenderGraph = true;
    bool EnableTransientAliasing = true;
    // Optional runtime pipeline asset. Empty uses the built-in renderer graph.
    std::string RenderGraphConfigPath;
};

struct BackendCapabilities
{
    std::string AdapterName = "Unknown";
    uint64_t DedicatedVideoMemoryBytes = 0;
    uint32_t MaxMsaaSamples = 1;
    uint32_t ActiveMsaaSamples = 1;
    float MaxAnisotropy = 1.0f;
    float ActiveAnisotropy = 1.0f;
    bool HardwareAccelerated = true;
    bool GpuTiming = false;
    bool GpuPipelineStatistics = false;
    bool GpuMemoryBudget = false;
    bool ImmediatePresent = false;
    bool AdaptivePresent = false;
    GpuCapabilityProfile Gpu;
};

struct BackendFrameStats
{
    bool GpuTimingAvailable = false;
    float GpuFrameMilliseconds = 0.0f;
    float GpuShadowMilliseconds = 0.0f;
    float GpuOcclusionMilliseconds = 0.0f;
    float GpuMainMilliseconds = 0.0f;
    float GpuPostMilliseconds = 0.0f;
    bool GpuOcclusionActive = false;
    bool GpuOcclusionHistoryReset = false;
    uint32_t GpuOcclusionCandidates = 0;
    uint32_t GpuOcclusionCulled = 0;
    uint32_t GpuOcclusionResultsConsumed = 0;
    uint32_t HiZMipLevels = 0;
    uint32_t OcclusionReadbackLatencyFrames = 0;
    bool GpuInstancingActive = false;
    uint32_t GpuInstanceCount = 0;
    uint32_t GpuInstanceBatchCount = 0;
    uint32_t GpuInstancedBatchCount = 0;
    uint32_t GpuDrawCallsSaved = 0;
};

// Backend-neutral lifetime counters used by the runtime diagnostics and the
// long-running stability suite. Vulkan reports allocator-exact bytes and
// allocation counts. OpenGL reports deterministic estimates for engine-owned
// objects because the API does not expose portable allocation sizes.
struct BackendResourceStats
{
    uint64_t DeviceLocalBytes = 0;
    uint64_t PeakDeviceLocalBytes = 0;
    uint64_t HostVisibleBytes = 0;
    uint64_t PeakHostVisibleBytes = 0;
    uint64_t LiveNativeAllocations = 0;
    uint64_t MeshResources = 0;
    uint64_t TextureResources = 0;
    uint64_t MaterialResources = 0;
};

enum class RenderBackendApi : uint8_t
{
    OpenGL,
    Vulkan
};

struct RenderViewportHandle
{
    uint64_t Value = 0;
    explicit operator bool() const noexcept { return Value != 0; }
    auto operator<=>(const RenderViewportHandle&) const = default;
};

struct RenderViewportDesc
{
    uint32_t Width = 1;
    uint32_t Height = 1;
    std::string Name = "Viewport";
};

// Backend-neutral token suitable for editor/UI integrations. NativeTexture is
// a GLuint on GL. Vulkan exposes its image view and sampler separately. The
// generation changes whenever resize replaces the native resource.
struct RenderTextureHandle
{
    RenderBackendApi Api = RenderBackendApi::OpenGL;
    RenderViewportHandle Viewport;
    uint64_t NativeTexture = 0;
    uint64_t NativeImageView = 0;
    uint64_t NativeSampler = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint64_t Generation = 0;
    explicit operator bool() const noexcept
    {
        return Width != 0 && Height != 0 &&
            (NativeTexture != 0 || NativeImageView != 0);
    }
};

// Minimal native context required by optional editor UI backends. Handles are
// intentionally opaque here so RendererCore does not acquire Vulkan headers.
struct NativeGraphicsContext
{
    RenderBackendApi Api = RenderBackendApi::OpenGL;
    void* Window = nullptr;
    uint64_t Instance = 0;
    uint64_t PhysicalDevice = 0;
    uint64_t Device = 0;
    uint64_t Queue = 0;
    uint32_t QueueFamily = 0;
    uint32_t ColorFormat = 0;
    uint32_t MinImageCount = 2;
    uint32_t ImageCount = 2;
};

struct NativeUiRenderContext
{
    RenderBackendApi Api = RenderBackendApi::OpenGL;
    uint64_t CommandBuffer = 0;
    uint64_t ColorImageView = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
};

using NativeUiRenderCallback = std::function<void(const NativeUiRenderContext&)>;

// Shared contract between the OpenGL and Vulkan backends. Kept intentionally
// small (immediate-mode-ish per-frame calls) rather than a full generic RHI
// (command buffers, pipeline objects, descriptor abstractions, ...) because
// building that abstraction well requires knowing both backends' real
// constraints first. Once the Vulkan backend is fleshed out and its actual
// resource lifetime/threading needs are clear, this is the seam to widen into
// a proper RHI (buffers/textures/pipelines as first-class objects shared by
// both backends) instead of guessing at one up front.
class IRenderBackend
{
  public:
    virtual ~IRenderBackend() = default;

    virtual void Init(Window& window, const RenderBackendConfig& config) = 0;
    virtual void Shutdown() = 0;

    virtual void Resize(int width, int height) = 0;

    virtual void RenderFrame(const RenderFrameData& frame) = 0;

    // Persistent offscreen views. Several handles may coexist; rendering one
    // never invalidates another view's output texture.
    virtual RenderViewportHandle CreateViewport(const RenderViewportDesc&) { return {}; }
    virtual bool ResizeViewport(RenderViewportHandle, uint32_t, uint32_t) { return false; }
    virtual void DestroyViewport(RenderViewportHandle) {}
    virtual bool RenderViewport(RenderViewportHandle, const RenderFrameData&) { return false; }
    virtual RenderTextureHandle GetViewportTexture(RenderViewportHandle) const { return {}; }

    virtual NativeGraphicsContext GetNativeGraphicsContext() const { return {}; }
    // Synchronization point for API-native extensions (such as editor UI)
    // which must release their GPU objects before backend Shutdown destroys
    // the device/context.
    virtual void WaitIdle() {}
    virtual void SetUiRenderCallback(NativeUiRenderCallback callback)
    {
        (void)callback;
    }

    // Saves the next presented frame as a PNG. Default: unsupported no-op.
    virtual void RequestScreenshot(const std::string& /*path*/) {}

    // Saves the next resolved scene-color target before exposure, bloom,
    // tonemapping and color grading as a linear Radiance HDR image. This is
    // primarily used by backend-parity and golden-image regression tests.
    virtual void RequestHdrScreenshot(const std::string& /*path*/) {}

    virtual BackendFrameStats GetFrameStats() const { return {}; }
    virtual BackendCapabilities GetCapabilities() const { return {}; }
    virtual BackendResourceStats GetResourceStats() const { return {}; }
    virtual PipelineCacheStatistics GetPipelineCacheStats() const { return {}; }
    virtual rendergraph::Statistics GetRenderGraphStats() const { return {}; }

    // API-neutral frame graph/resource metadata plus an explicit frozen
    // preview capture. Capture may synchronize the GPU and is therefore only
    // called while the in-engine frame debugger is open.
    virtual debug::FrameDebugSnapshot GetFrameDebugSnapshot() const { return {}; }
    virtual bool CaptureFrameDebugResource(uint64_t /*resourceId*/, uint32_t /*mipLevel*/,
                                           uint32_t /*layer*/,
                                           debug::FrameDebugPreview& /*preview*/)
    {
        return false;
    }

    // Returns false when the requested mode is unavailable and a safe fallback
    // was selected. Changing this may recreate a native swapchain.
    virtual bool SetPresentMode(PresentMode /*mode*/) { return false; }

    virtual const char* Name() const = 0;
};

} // namespace engine
