#pragma once

#include <cstdint>
#include <string>

#include "engine/debug/FrameDebugger.h"

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
};

struct BackendFrameStats
{
    bool GpuTimingAvailable = false;
    float GpuFrameMilliseconds = 0.0f;
    float GpuShadowMilliseconds = 0.0f;
    float GpuMainMilliseconds = 0.0f;
    float GpuPostMilliseconds = 0.0f;
};

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

    // Saves the next presented frame as a PNG. Default: unsupported no-op.
    virtual void RequestScreenshot(const std::string& /*path*/) {}

    // Saves the next resolved scene-color target before exposure, bloom,
    // tonemapping and color grading as a linear Radiance HDR image. This is
    // primarily used by backend-parity and golden-image regression tests.
    virtual void RequestHdrScreenshot(const std::string& /*path*/) {}

    virtual BackendFrameStats GetFrameStats() const { return {}; }
    virtual BackendCapabilities GetCapabilities() const { return {}; }

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
