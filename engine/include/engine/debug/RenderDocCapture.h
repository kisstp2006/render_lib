#pragma once

#include <cstdint>
#include <string>

namespace engine::debug
{

struct RenderDocCaptureConfig
{
    bool Enabled = false;
    bool RequireAvailable = false;
    bool QuitAfterCapture = true;
    bool ApiValidation = false;
    // Filled by Application from Window.api. When a portable RenderDoc DLL is
    // supplied, its Vulkan layer manifest is exposed before instance creation.
    bool PrepareVulkanLayer = false;
    uint64_t FrameIndex = 5;
    float FixedDeltaSeconds = 1.0f / 60.0f;
    std::string CapturePathTemplate = "captures/renderer";
    std::string LibraryPath;
    std::string CaptureTitle;
};

// Optional in-application RenderDoc capture controller. RenderDoc is discovered
// dynamically, so ordinary runtime builds have no link-time dependency on it.
class RenderDocCapture
{
  public:
    RenderDocCapture() = default;
    ~RenderDocCapture();
    RenderDocCapture(const RenderDocCapture&) = delete;
    RenderDocCapture& operator=(const RenderDocCapture&) = delete;

    bool Initialize(const RenderDocCaptureConfig& config);
    void Shutdown();

    bool BeginFrame(uint64_t frameIndex);
    bool EndFrame();

    const RenderDocCaptureConfig& Config() const { return m_config; }
    bool IsRequested() const { return m_config.Enabled; }
    bool IsAvailable() const { return m_api != nullptr; }
    bool IsCapturing() const { return m_captureActive; }
    bool CaptureCompleted() const { return m_captureCompleted; }
    const std::string& LastCapturePath() const { return m_lastCapturePath; }
    const std::string& LastError() const { return m_lastError; }

  private:
    void UpdateLastCapturePath(uint32_t previousCaptureCount);

    RenderDocCaptureConfig m_config;
    void* m_module = nullptr;
    void* m_api = nullptr;
    uint32_t m_captureCountBefore = 0;
    bool m_moduleOwned = false;
    bool m_captureActive = false;
    bool m_captureCompleted = false;
    std::string m_lastCapturePath;
    std::string m_lastError;
};

} // namespace engine::debug
