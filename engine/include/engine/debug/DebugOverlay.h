#pragma once

#include "engine/backend/IRenderBackend.h"
#include "engine/profiling/CpuProfiler.h"

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
};

// Immutable for the duration of RenderFrame. Backends upload this small RGBA8
// image and composite it after all image-quality passes, so debug text remains
// crisp and identical without depending on ImGui or a native graphics API.
struct DebugOverlayImage
{
    static constexpr uint32_t Width = 480;
    static constexpr uint32_t Height = 320;

    std::vector<uint8_t> Pixels;
    uint64_t Revision = 0;
};

class DebugOverlay
{
public:
    DebugOverlay();

    void SetVisible(bool visible) { m_visible = visible; }
    void Toggle() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }

    // Persistent application-provided values, grouped like ezEngine's info
    // text. Setting the same group/key replaces the previous value.
    void SetValue(std::string group, std::string key, std::string value);
    void RemoveValue(std::string_view group, std::string_view key);
    void ClearValues();

    void Update(const DebugOverlayMetrics& metrics,
                const profiling::CpuProfileSnapshot& cpuProfile);
    const DebugOverlayImage& Image() const { return m_image; }

private:
    struct Color
    {
        uint8_t R = 255, G = 255, B = 255, A = 255;
    };

    void Clear(Color color);
    void Pixel(int x, int y, Color color);
    void Rectangle(int x, int y, int width, int height, Color color);
    void Line(int x0, int y0, int x1, int y1, Color color);
    void Text(int x, int y, std::string_view text, Color color, int scale = 2);
    void Graph(int x, int y, int width, int height);
    static std::string Number(double value, int precision = 2);

    bool m_visible = false;
    DebugOverlayImage m_image;
    mutable std::mutex m_valuesMutex;
    std::map<std::string, std::map<std::string, std::string>> m_values;
    std::vector<float> m_frameHistory;
    float m_smoothedFrameMilliseconds = 16.67f;
};

} // namespace engine::debug
