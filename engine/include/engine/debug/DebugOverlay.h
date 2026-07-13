#pragma once

#include "engine/backend/IRenderBackend.h"
#include "engine/debug/FrameDebugger.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/profiling/GpuProfiler.h"
#include "engine/profiling/MemoryProfiler.h"

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace engine::debug {

struct DebugOverlayMetrics
{
    std::string BackendName;
    std::string AntiAliasing;
    int ViewWidth = 1;
    int ViewHeight = 1;
    float DeltaSeconds = 0.0f;
    float Exposure = 1.0f;
    uint64_t FrameIndex = 0;
    uint64_t TriangleCount = 0;
    uint32_t ObjectCount = 0;
    uint32_t PointLightCount = 0;
    uint32_t SpotLightCount = 0;
    uint32_t AreaLightCount = 0;
    BackendFrameStats Backend{};
    BackendCapabilities Capabilities{};
};

enum class DebugOverlayPlacement : uint8_t
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

struct DebugOverlayLayer
{
    uint32_t SourceX = 0;
    uint32_t SourceY = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    DebugOverlayPlacement Placement = DebugOverlayPlacement::TopLeft;
    int32_t MarginX = 10;
    int32_t MarginY = 10;
};

struct DebugOverlayDrawRect
{
    int32_t X = 0;
    int32_t Y = 0;
    uint32_t SourceX = 0;
    uint32_t SourceY = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
};

DebugOverlayDrawRect ResolveDebugOverlayLayer(const DebugOverlayLayer& layer,
                                              uint32_t targetWidth,
                                              uint32_t targetHeight);

// Immutable for the duration of RenderFrame. Backends upload this small RGBA8
// image and composite it after all image-quality passes, so debug text remains
// crisp and identical without depending on ImGui or a native graphics API.
struct DebugOverlayImage
{
    static constexpr uint32_t DetailedWidth = 480;
    static constexpr uint32_t DetailedHeight = 320;
    static constexpr uint32_t FrameDebuggerWidth = 960;
    static constexpr uint32_t FrameDebuggerHeight = 540;
    static constexpr uint32_t TextureWidth = FrameDebuggerWidth;
    static constexpr uint32_t TextureHeight = FrameDebuggerHeight;
    static constexpr uint32_t MaximumLayers = 4;
    // Compatibility aliases for backend texture allocation sites.
    static constexpr uint32_t Width = TextureWidth;
    static constexpr uint32_t Height = TextureHeight;

    std::vector<uint8_t> Pixels;
    std::array<DebugOverlayLayer, MaximumLayers> Layers{};
    uint32_t LayerCount = 0;
    uint64_t Revision = 0;
};

class DebugOverlay
{
public:
    DebugOverlay();

    void SetVisible(bool visible) { m_visible = visible; }
    void Toggle() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetRuntimeMonitorsVisible(bool visible) { m_runtimeMonitorsVisible = visible; }
    bool RuntimeMonitorsVisible() const { return m_runtimeMonitorsVisible; }
    void SetFrameDebuggerVisible(bool visible);
    void ToggleFrameDebugger() { SetFrameDebuggerVisible(!m_frameDebuggerVisible); }
    bool FrameDebuggerVisible() const { return m_frameDebuggerVisible; }
    bool HasVisibleContent() const
    {
        return m_visible || m_runtimeMonitorsVisible || m_frameDebuggerVisible;
    }
    void SetFrameDebugSnapshot(FrameDebugSnapshot snapshot);
    void MoveFrameDebugPass(int delta);
    void MoveFrameDebugResource(int delta);
    void MoveFrameDebugMip(int delta);
    void MoveFrameDebugLayer(int delta);
    void RequestFrameDebugRefresh() { m_frameDebugCaptureRequested = true; }
    bool GetFrameDebugCaptureRequest(uint64_t& resourceId, uint32_t& mipLevel,
                                     uint32_t& layer) const;
    void SetFrameDebugPreview(FrameDebugPreview preview);
    void SetRuntimeMonitorPlacements(DebugOverlayPlacement cpuMemory,
                                     DebugOverlayPlacement gpu);

    // Persistent application-provided values, grouped like ezEngine's info
    // text. Setting the same group/key replaces the previous value.
    void SetValue(std::string group, std::string key, std::string value);
    void RemoveValue(std::string_view group, std::string_view key);
    void ClearValues();

    void Update(const DebugOverlayMetrics& metrics,
                const profiling::CpuProfileSnapshot& cpuProfile,
                const profiling::MemoryProfileSnapshot& memoryProfile = {},
                const profiling::GpuProfileSnapshot& gpuProfile = {});
    const DebugOverlayImage& Image() const { return m_image; }

private:
    struct Color
    {
        uint8_t R = 255, G = 255, B = 255, A = 255;
    };

    void Clear(Color color);
    void EnsureImageStorage();
    void BeginRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void AddLayer(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  DebugOverlayPlacement placement);
    void Pixel(int x, int y, Color color);
    void Rectangle(int x, int y, int width, int height, Color color);
    void Line(int x0, int y0, int x1, int y1, Color color);
    void Text(int x, int y, std::string_view text, Color color, int scale = 2);
    void Graph(int x, int y, int width, int height);
    void Image(int x, int y, int width, int height, const FrameDebugPreview& image);
    void DrawDetailedPanel(const DebugOverlayMetrics& metrics,
                           const profiling::CpuProfileSnapshot& cpuProfile,
                           const profiling::MemoryProfileSnapshot& memoryProfile,
                           const profiling::GpuProfileSnapshot& gpuProfile);
    void DrawCpuMemoryMonitor(const profiling::MemoryProfileSnapshot& memoryProfile);
    void DrawGpuMonitor(const DebugOverlayMetrics& metrics,
                        const profiling::GpuProfileSnapshot& gpuProfile);
    void DrawFrameDebuggerPanel(const profiling::GpuProfileSnapshot& gpuProfile);
    static std::string Number(double value, int precision = 2);

    bool m_visible = false;
    bool m_runtimeMonitorsVisible = false;
    bool m_frameDebuggerVisible = false;
    bool m_frameDebugCaptureRequested = false;
    DebugOverlayPlacement m_cpuMemoryPlacement = DebugOverlayPlacement::TopRight;
    DebugOverlayPlacement m_gpuPlacement = DebugOverlayPlacement::BottomRight;
    DebugOverlayImage m_image;
    mutable std::mutex m_valuesMutex;
    std::map<std::string, std::map<std::string, std::string>> m_values;
    std::vector<float> m_frameHistory;
    std::vector<float> m_gpuFrameHistory;
    FrameDebugSnapshot m_frameDebugSnapshot;
    FrameDebugPreview m_frameDebugPreview;
    size_t m_frameDebugPass = 0;
    size_t m_frameDebugResource = 0;
    uint32_t m_frameDebugMip = 0;
    uint32_t m_frameDebugLayer = 0;
    float m_smoothedFrameMilliseconds = 16.67f;
    uint32_t m_regionX = 0;
    uint32_t m_regionY = 0;
    uint32_t m_regionWidth = DebugOverlayImage::DetailedWidth;
    uint32_t m_regionHeight = DebugOverlayImage::DetailedHeight;
};

} // namespace engine::debug
