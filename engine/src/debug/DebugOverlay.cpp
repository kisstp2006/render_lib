#include "engine/debug/DebugOverlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace engine::debug {
namespace {

using Glyph = std::array<uint8_t, 5>;

Glyph FontGlyph(char character)
{
    if (character >= 'a' && character <= 'z')
        character = static_cast<char>(character - 'a' + 'A');
    switch (character)
    {
    case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00};
    case '!': return {0x00, 0x00, 0x5F, 0x00, 0x00};
    case '"': return {0x00, 0x07, 0x00, 0x07, 0x00};
    case '#': return {0x14, 0x7F, 0x14, 0x7F, 0x14};
    case '%': return {0x23, 0x13, 0x08, 0x64, 0x62};
    case '&': return {0x36, 0x49, 0x55, 0x22, 0x50};
    case 39: return {0x00, 0x05, 0x03, 0x00, 0x00};
    case '(': return {0x00, 0x1C, 0x22, 0x41, 0x00};
    case ')': return {0x00, 0x41, 0x22, 0x1C, 0x00};
    case '*': return {0x14, 0x08, 0x3E, 0x08, 0x14};
    case '+': return {0x08, 0x08, 0x3E, 0x08, 0x08};
    case ',': return {0x00, 0x50, 0x30, 0x00, 0x00};
    case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
    case '.': return {0x00, 0x60, 0x60, 0x00, 0x00};
    case '/': return {0x20, 0x10, 0x08, 0x04, 0x02};
    case '0': return {0x3E, 0x51, 0x49, 0x45, 0x3E};
    case '1': return {0x00, 0x42, 0x7F, 0x40, 0x00};
    case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
    case '3': return {0x21, 0x41, 0x45, 0x4B, 0x31};
    case '4': return {0x18, 0x14, 0x12, 0x7F, 0x10};
    case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
    case '6': return {0x3C, 0x4A, 0x49, 0x49, 0x30};
    case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
    case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
    case '9': return {0x06, 0x49, 0x49, 0x29, 0x1E};
    case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
    case ';': return {0x00, 0x56, 0x36, 0x00, 0x00};
    case '<': return {0x08, 0x14, 0x22, 0x41, 0x00};
    case '=': return {0x14, 0x14, 0x14, 0x14, 0x14};
    case '>': return {0x00, 0x41, 0x22, 0x14, 0x08};
    case '?': return {0x02, 0x01, 0x51, 0x09, 0x06};
    case '@': return {0x32, 0x49, 0x79, 0x41, 0x3E};
    case 'A': return {0x7E, 0x11, 0x11, 0x11, 0x7E};
    case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
    case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
    case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
    case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
    case 'F': return {0x7F, 0x09, 0x09, 0x09, 0x01};
    case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
    case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
    case 'I': return {0x00, 0x41, 0x7F, 0x41, 0x00};
    case 'J': return {0x20, 0x40, 0x41, 0x3F, 0x01};
    case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
    case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
    case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
    case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
    case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
    case 'Q': return {0x3E, 0x41, 0x51, 0x21, 0x5E};
    case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
    case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
    case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
    case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
    case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
    case 'W': return {0x3F, 0x40, 0x38, 0x40, 0x3F};
    case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
    case 'Y': return {0x07, 0x08, 0x70, 0x08, 0x07};
    case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
    case '[': return {0x00, 0x7F, 0x41, 0x41, 0x00};
    case '\\': return {0x02, 0x04, 0x08, 0x10, 0x20};
    case ']': return {0x00, 0x41, 0x41, 0x7F, 0x00};
    case '^': return {0x04, 0x02, 0x01, 0x02, 0x04};
    case '_': return {0x40, 0x40, 0x40, 0x40, 0x40};
    default: return {0x02, 0x01, 0x51, 0x09, 0x06};
    }
}

std::string Shorten(std::string value, size_t maximum)
{
    if (value.size() <= maximum)
        return value;
    if (maximum <= 3)
        return value.substr(0, maximum);
    return value.substr(0, maximum - 3) + "...";
}

constexpr uint32_t kCpuMonitorX = 488;
constexpr uint32_t kCpuMonitorY = 0;
constexpr uint32_t kCpuMonitorWidth = 300;
constexpr uint32_t kCpuMonitorHeight = 96;
constexpr uint32_t kGpuMonitorX = 488;
constexpr uint32_t kGpuMonitorY = 104;
constexpr uint32_t kGpuMonitorWidth = 300;
constexpr uint32_t kGpuMonitorHeight = 148;

} // namespace

DebugOverlayDrawRect ResolveDebugOverlayLayer(const DebugOverlayLayer& layer,
                                              uint32_t targetWidth,
                                              uint32_t targetHeight)
{
    DebugOverlayDrawRect result;
    result.SourceX = layer.SourceX;
    result.SourceY = layer.SourceY;
    if (targetWidth == 0 || targetHeight == 0 || layer.Width == 0 || layer.Height == 0)
        return result;

    const int32_t marginX = std::max(layer.MarginX, 0);
    const int32_t marginY = std::max(layer.MarginY, 0);
    const int32_t targetW = static_cast<int32_t>(targetWidth);
    const int32_t targetH = static_cast<int32_t>(targetHeight);
    const int32_t layerW = static_cast<int32_t>(layer.Width);
    const int32_t layerH = static_cast<int32_t>(layer.Height);
    const bool right = layer.Placement == DebugOverlayPlacement::TopRight ||
                       layer.Placement == DebugOverlayPlacement::BottomRight;
    const bool bottom = layer.Placement == DebugOverlayPlacement::BottomLeft ||
                        layer.Placement == DebugOverlayPlacement::BottomRight;
    result.X = right ? std::max(targetW - marginX - layerW, 0) : marginX;
    result.Y = bottom ? std::max(targetH - marginY - layerH, 0) : marginY;
    if (result.X >= targetW || result.Y >= targetH)
        return result;
    result.Width = std::min(layer.Width, static_cast<uint32_t>(targetW - result.X));
    result.Height = std::min(layer.Height, static_cast<uint32_t>(targetH - result.Y));
    return result;
}

DebugOverlay::DebugOverlay()
{
    m_frameHistory.reserve(120);
    m_gpuFrameHistory.reserve(120);
}

void DebugOverlay::SetRuntimeMonitorPlacements(DebugOverlayPlacement cpuMemory,
                                               DebugOverlayPlacement gpu)
{
    m_cpuMemoryPlacement = cpuMemory;
    m_gpuPlacement = gpu;
}

void DebugOverlay::SetFrameDebuggerVisible(bool visible)
{
    if (m_frameDebuggerVisible == visible)
        return;
    m_frameDebuggerVisible = visible;
    if (visible)
        m_frameDebugCaptureRequested = true;
}

void DebugOverlay::SetFrameDebugSnapshot(FrameDebugSnapshot snapshot)
{
    const FrameDebugResource* previousResource = m_frameDebugSnapshot.Resources.empty()
        ? nullptr : &m_frameDebugSnapshot.Resources[std::min(m_frameDebugResource,
            m_frameDebugSnapshot.Resources.size() - 1)];
    const uint64_t selectedId = previousResource ? previousResource->Id : 0;
    const uint32_t previousWidth = previousResource ? previousResource->Width : 0;
    const uint32_t previousHeight = previousResource ? previousResource->Height : 0;
    const bool previousPreviewable = previousResource && previousResource->Previewable;
    m_frameDebugSnapshot = std::move(snapshot);
    m_frameDebugPass = m_frameDebugSnapshot.Passes.empty()
        ? 0 : std::min(m_frameDebugPass, m_frameDebugSnapshot.Passes.size() - 1);
    if (m_frameDebugSnapshot.Resources.empty())
    {
        m_frameDebugResource = 0;
        return;
    }
    const auto found = std::find_if(m_frameDebugSnapshot.Resources.begin(),
        m_frameDebugSnapshot.Resources.end(), [selectedId](const FrameDebugResource& resource)
        { return resource.Id == selectedId; });
    const size_t newIndex = found == m_frameDebugSnapshot.Resources.end()
        ? std::min(m_frameDebugResource, m_frameDebugSnapshot.Resources.size() - 1)
        : static_cast<size_t>(std::distance(m_frameDebugSnapshot.Resources.begin(), found));
    if (newIndex != m_frameDebugResource || selectedId == 0)
    {
        m_frameDebugCaptureRequested = true;
        m_frameDebugPreview = {};
    }
    m_frameDebugResource = newIndex;
    const FrameDebugResource& resource = m_frameDebugSnapshot.Resources[m_frameDebugResource];
    if (resource.Width != previousWidth || resource.Height != previousHeight ||
        resource.Previewable != previousPreviewable)
    {
        m_frameDebugCaptureRequested = true;
        m_frameDebugPreview = {};
    }
    m_frameDebugMip = std::min(m_frameDebugMip, resource.MipLevels - 1);
    m_frameDebugLayer = std::min(m_frameDebugLayer, resource.Layers - 1);
}

namespace {

size_t WrappedIndex(size_t current, int delta, size_t count)
{
    if (count == 0)
        return 0;
    const int64_t value = static_cast<int64_t>(current) + delta;
    const int64_t modulus = static_cast<int64_t>(count);
    return static_cast<size_t>((value % modulus + modulus) % modulus);
}

} // namespace

void DebugOverlay::MoveFrameDebugPass(int delta)
{
    m_frameDebugPass = WrappedIndex(m_frameDebugPass, delta, m_frameDebugSnapshot.Passes.size());
}

void DebugOverlay::MoveFrameDebugResource(int delta)
{
    if (m_frameDebugSnapshot.Resources.empty())
        return;
    m_frameDebugResource = WrappedIndex(m_frameDebugResource, delta,
                                        m_frameDebugSnapshot.Resources.size());
    m_frameDebugMip = 0;
    m_frameDebugLayer = 0;
    m_frameDebugPreview = {};
    m_frameDebugCaptureRequested = true;
}

void DebugOverlay::MoveFrameDebugMip(int delta)
{
    if (m_frameDebugSnapshot.Resources.empty())
        return;
    const FrameDebugResource& resource = m_frameDebugSnapshot.Resources[m_frameDebugResource];
    m_frameDebugMip = static_cast<uint32_t>(WrappedIndex(m_frameDebugMip, delta,
                                                         resource.MipLevels));
    m_frameDebugPreview = {};
    m_frameDebugCaptureRequested = true;
}

void DebugOverlay::MoveFrameDebugLayer(int delta)
{
    if (m_frameDebugSnapshot.Resources.empty())
        return;
    const FrameDebugResource& resource = m_frameDebugSnapshot.Resources[m_frameDebugResource];
    m_frameDebugLayer = static_cast<uint32_t>(WrappedIndex(m_frameDebugLayer, delta,
                                                           resource.Layers));
    m_frameDebugPreview = {};
    m_frameDebugCaptureRequested = true;
}

bool DebugOverlay::GetFrameDebugCaptureRequest(uint64_t& resourceId, uint32_t& mipLevel,
                                               uint32_t& layer) const
{
    if (!m_frameDebuggerVisible || !m_frameDebugCaptureRequested ||
        m_frameDebugSnapshot.Resources.empty())
        return false;
    const FrameDebugResource& resource = m_frameDebugSnapshot.Resources[m_frameDebugResource];
    if (!resource.Previewable)
        return false;
    resourceId = resource.Id;
    mipLevel = m_frameDebugMip;
    layer = m_frameDebugLayer;
    return true;
}

void DebugOverlay::SetFrameDebugPreview(FrameDebugPreview preview)
{
    m_frameDebugPreview = std::move(preview);
    m_frameDebugCaptureRequested = false;
}

void DebugOverlay::SetValue(std::string group, std::string key, std::string value)
{
    std::scoped_lock lock(m_valuesMutex);
    m_values[std::move(group)][std::move(key)] = std::move(value);
}

void DebugOverlay::RemoveValue(std::string_view group, std::string_view key)
{
    std::scoped_lock lock(m_valuesMutex);
    const auto groupFound = m_values.find(std::string(group));
    if (groupFound == m_values.end())
        return;
    groupFound->second.erase(std::string(key));
    if (groupFound->second.empty())
        m_values.erase(groupFound);
}

void DebugOverlay::ClearValues()
{
    std::scoped_lock lock(m_valuesMutex);
    m_values.clear();
}

void DebugOverlay::Clear(Color color)
{
    for (size_t pixel = 0; pixel < m_image.Pixels.size(); pixel += 4)
    {
        m_image.Pixels[pixel + 0] = color.R;
        m_image.Pixels[pixel + 1] = color.G;
        m_image.Pixels[pixel + 2] = color.B;
        m_image.Pixels[pixel + 3] = color.A;
    }
}

void DebugOverlay::EnsureImageStorage()
{
    const size_t byteCount = static_cast<size_t>(DebugOverlayImage::TextureWidth) *
                             DebugOverlayImage::TextureHeight * 4;
    if (m_image.Pixels.size() != byteCount)
        m_image.Pixels.resize(byteCount);
}

void DebugOverlay::BeginRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    m_regionX = x;
    m_regionY = y;
    m_regionWidth = width;
    m_regionHeight = height;
}

void DebugOverlay::AddLayer(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                            DebugOverlayPlacement placement)
{
    if (m_image.LayerCount >= m_image.Layers.size())
        return;
    m_image.Layers[m_image.LayerCount++] = {x, y, width, height, placement, 10, 10};
}

void DebugOverlay::Pixel(int x, int y, Color color)
{
    if (x < 0 || y < 0 || x >= static_cast<int>(m_regionWidth)
        || y >= static_cast<int>(m_regionHeight))
        return;
    const uint32_t atlasX = m_regionX + static_cast<uint32_t>(x);
    const uint32_t atlasY = m_regionY + static_cast<uint32_t>(y);
    if (atlasX >= DebugOverlayImage::TextureWidth || atlasY >= DebugOverlayImage::TextureHeight)
        return;
    const size_t index = (static_cast<size_t>(atlasY) * DebugOverlayImage::TextureWidth
                        + static_cast<size_t>(atlasX)) * 4;
    m_image.Pixels[index + 0] = color.R;
    m_image.Pixels[index + 1] = color.G;
    m_image.Pixels[index + 2] = color.B;
    m_image.Pixels[index + 3] = color.A;
}

void DebugOverlay::Rectangle(int x, int y, int width, int height, Color color)
{
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column)
            Pixel(column, row, color);
}

void DebugOverlay::Line(int x0, int y0, int x1, int y1, Color color)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true)
    {
        Pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        const int twiceError = error * 2;
        if (twiceError >= dy) { error += dy; x0 += sx; }
        if (twiceError <= dx) { error += dx; y0 += sy; }
    }
}

void DebugOverlay::Text(int x, int y, std::string_view text, Color color, int scale)
{
    int cursor = x;
    for (const char character : text)
    {
        const Glyph glyph = FontGlyph(character);
        for (int column = 0; column < 5; ++column)
            for (int row = 0; row < 7; ++row)
                if ((glyph[column] & (1u << row)) != 0)
                    Rectangle(cursor + column * scale, y + row * scale, scale, scale, color);
        cursor += 6 * scale;
        if (cursor >= static_cast<int>(m_regionWidth) - 4)
            break;
    }
}

std::string DebugOverlay::Number(double value, int precision)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

void DebugOverlay::Graph(int x, int y, int width, int height)
{
    const Color grid{61, 72, 91, 255};
    const Color good{65, 220, 170, 255};
    const Color slow{255, 187, 82, 255};
    Rectangle(x, y, width, height, {10, 14, 21, 255});
    const int target60 = y + height - static_cast<int>(16.67f / 40.0f * height);
    Line(x, target60, x + width - 1, target60, grid);
    if (m_frameHistory.size() < 2)
        return;
    const size_t first = m_frameHistory.size() > static_cast<size_t>(width)
        ? m_frameHistory.size() - static_cast<size_t>(width) : 0;
    const size_t count = m_frameHistory.size() - first;
    for (size_t sample = 1; sample < count; ++sample)
    {
        const float previous = std::clamp(m_frameHistory[first + sample - 1], 0.0f, 40.0f);
        const float current = std::clamp(m_frameHistory[first + sample], 0.0f, 40.0f);
        const int px = x + static_cast<int>((sample - 1) * static_cast<size_t>(width - 1) / (count - 1));
        const int cx = x + static_cast<int>(sample * static_cast<size_t>(width - 1) / (count - 1));
        const int py = y + height - 1 - static_cast<int>(previous / 40.0f * (height - 1));
        const int cy = y + height - 1 - static_cast<int>(current / 40.0f * (height - 1));
        Line(px, py, cx, cy, current <= 18.0f ? good : slow);
    }
    if (m_gpuFrameHistory.size() >= 2)
    {
        const Color gpu{112, 173, 255, 255};
        const size_t gpuFirst = m_gpuFrameHistory.size() > static_cast<size_t>(width)
            ? m_gpuFrameHistory.size() - static_cast<size_t>(width) : 0;
        const size_t gpuCount = m_gpuFrameHistory.size() - gpuFirst;
        for (size_t sample = 1; sample < gpuCount; ++sample)
        {
            const float previous = std::clamp(m_gpuFrameHistory[gpuFirst + sample - 1], 0.0f, 40.0f);
            const float current = std::clamp(m_gpuFrameHistory[gpuFirst + sample], 0.0f, 40.0f);
            const int px = x + static_cast<int>((sample - 1) * static_cast<size_t>(width - 1) / (gpuCount - 1));
            const int cx = x + static_cast<int>(sample * static_cast<size_t>(width - 1) / (gpuCount - 1));
            const int py = y + height - 1 - static_cast<int>(previous / 40.0f * (height - 1));
            const int cy = y + height - 1 - static_cast<int>(current / 40.0f * (height - 1));
            Line(px, py, cx, cy, gpu);
        }
    }
}

void DebugOverlay::Image(int x, int y, int width, int height,
                         const FrameDebugPreview& image)
{
    if (!image.Valid() || width <= 0 || height <= 0)
        return;
    for (int targetY = 0; targetY < height; ++targetY)
    {
        const uint32_t sourceY = std::min(
            static_cast<uint32_t>(targetY) * image.Height / static_cast<uint32_t>(height),
            image.Height - 1);
        for (int targetX = 0; targetX < width; ++targetX)
        {
            const uint32_t sourceX = std::min(
                static_cast<uint32_t>(targetX) * image.Width / static_cast<uint32_t>(width),
                image.Width - 1);
            const size_t source = (static_cast<size_t>(sourceY) * image.Width + sourceX) * 4;
            Pixel(x + targetX, y + targetY,
                  {image.Pixels[source], image.Pixels[source + 1],
                   image.Pixels[source + 2], image.Pixels[source + 3]});
        }
    }
}

void DebugOverlay::Update(const DebugOverlayMetrics& metrics,
                           const profiling::CpuProfileSnapshot& cpuProfile,
                           const profiling::MemoryProfileSnapshot& memoryProfile,
                           const profiling::GpuProfileSnapshot& gpuProfile)
{
    if (!HasVisibleContent())
        return;

    const float frameMilliseconds = std::clamp(metrics.DeltaSeconds * 1000.0f, 0.0f, 1000.0f);
    if (frameMilliseconds > 0.0f)
        m_smoothedFrameMilliseconds += (frameMilliseconds - m_smoothedFrameMilliseconds) * 0.10f;
    m_frameHistory.push_back(frameMilliseconds);
    if (m_frameHistory.size() > 120)
        m_frameHistory.erase(m_frameHistory.begin());
    if (gpuProfile.TimingAvailable)
    {
        m_gpuFrameHistory.push_back(gpuProfile.FrameMilliseconds);
        if (m_gpuFrameHistory.size() > 120)
            m_gpuFrameHistory.erase(m_gpuFrameHistory.begin());
    }

    EnsureImageStorage();
    Clear({0, 0, 0, 0});
    m_image.LayerCount = 0;
    if (m_frameDebuggerVisible)
    {
        BeginRegion(0, 0, DebugOverlayImage::FrameDebuggerWidth,
                    DebugOverlayImage::FrameDebuggerHeight);
        DrawFrameDebuggerPanel(gpuProfile);
        AddLayer(0, 0, DebugOverlayImage::FrameDebuggerWidth,
                 DebugOverlayImage::FrameDebuggerHeight, DebugOverlayPlacement::TopLeft);
    }
    else if (m_visible)
    {
        BeginRegion(0, 0, DebugOverlayImage::DetailedWidth,
                    DebugOverlayImage::DetailedHeight);
        DrawDetailedPanel(metrics, cpuProfile, memoryProfile, gpuProfile);
        AddLayer(0, 0, DebugOverlayImage::DetailedWidth,
                 DebugOverlayImage::DetailedHeight, DebugOverlayPlacement::TopLeft);
    }
    // The full frame-debugger layer owns the whole atlas. Its header already
    // contains the relevant timing/memory context, so persistent cards are
    // temporarily suppressed instead of being baked into its preview area.
    if (m_runtimeMonitorsVisible && !m_frameDebuggerVisible)
    {
        DrawCpuMemoryMonitor(memoryProfile);
        DrawGpuMonitor(metrics, gpuProfile);
    }
    ++m_image.Revision;
}

void DebugOverlay::DrawFrameDebuggerPanel(
    const profiling::GpuProfileSnapshot& gpuProfile)
{
    const Color background{12, 16, 24, 246};
    const Color panel{20, 26, 38, 255};
    const Color text{224, 232, 244, 255};
    const Color dim{137, 151, 174, 255};
    const Color accent{65, 220, 170, 255};
    const Color heading{112, 173, 255, 255};
    const Color input{245, 186, 73, 255};
    const Color output{84, 211, 255, 255};
    Rectangle(0, 0, static_cast<int>(DebugOverlayImage::FrameDebuggerWidth),
              static_cast<int>(DebugOverlayImage::FrameDebuggerHeight), background);
    Rectangle(0, 0, static_cast<int>(DebugOverlayImage::FrameDebuggerWidth), 4, accent);
    Text(14, 12, "FRAME DEBUGGER", text);
    Text(188, 14, Shorten(m_frameDebugSnapshot.BackendName, 28), heading, 1);
    Text(700, 14, "F4 HIDE  R CAPTURE", dim, 1);
    Text(14, 32, "LEFT/RIGHT PASS  UP/DOWN RESOURCE  ,/. MIP  [/] LAYER", dim, 1);

    Rectangle(10, 50, 326, 478, panel);
    Rectangle(344, 50, 606, 184, panel);
    Rectangle(344, 242, 606, 286, panel);
    Text(22, 60, "RENDER PASSES", heading, 1);
    Text(356, 60, "RESOURCES", heading, 1);

    int passY = 80;
    const size_t passFirst = m_frameDebugPass > 12 ? m_frameDebugPass - 12 : 0;
    for (size_t index = passFirst;
         index < m_frameDebugSnapshot.Passes.size() && passY <= 276; ++index)
    {
        const FrameDebugPass& pass = m_frameDebugSnapshot.Passes[index];
        if (index == m_frameDebugPass)
            Rectangle(16, passY - 3, 314, 16, {39, 63, 79, 255});
        Text(22, passY, (index == m_frameDebugPass ? "> " : "  ") +
                         Shorten(pass.Name, 30), index == m_frameDebugPass ? text : dim, 1);
        const auto timing = std::find_if(gpuProfile.Passes.begin(), gpuProfile.Passes.end(),
            [&pass](const profiling::GpuPassSummary& value) { return value.Name == pass.Name; });
        if (timing != gpuProfile.Passes.end())
            Text(272, passY, Number(timing->LatestMilliseconds, 2), accent, 1);
        else if (pass.TimingAvailable)
            Text(272, passY, Number(pass.GpuMilliseconds, 2), accent, 1);
        else
            Text(284, passY, "--", dim, 1);
        passY += 16;
    }

    const FrameDebugPass* selectedPass = m_frameDebugSnapshot.Passes.empty()
        ? nullptr : &m_frameDebugSnapshot.Passes[m_frameDebugPass];
    Text(22, 304, "SELECTED PASS", heading, 1);
    if (selectedPass)
    {
        Text(22, 322, Shorten(selectedPass->Name, 42), text, 1);
        Text(22, 340, "INPUTS " + std::to_string(selectedPass->Inputs.size()) +
                      "  OUTPUTS " + std::to_string(selectedPass->Outputs.size()), dim, 1);
        int y = 360;
        for (const uint64_t id : selectedPass->Inputs)
        {
            const FrameDebugResource* resource = FindFrameDebugResource(m_frameDebugSnapshot, id);
            if (resource && y < 430)
            {
                Text(22, y, "I  " + Shorten(resource->Name, 36), input, 1);
                y += 14;
            }
        }
        for (const uint64_t id : selectedPass->Outputs)
        {
            const FrameDebugResource* resource = FindFrameDebugResource(m_frameDebugSnapshot, id);
            if (resource && y < 506)
            {
                Text(22, y, "O  " + Shorten(resource->Name, 36), output, 1);
                y += 14;
            }
        }
    }

    int resourceY = 80;
    const size_t resourceFirst = m_frameDebugResource > 7 ? m_frameDebugResource - 7 : 0;
    for (size_t index = resourceFirst;
         index < m_frameDebugSnapshot.Resources.size() && resourceY <= 194; ++index)
    {
        const FrameDebugResource& resource = m_frameDebugSnapshot.Resources[index];
        if (index == m_frameDebugResource)
            Rectangle(350, resourceY - 3, 594, 16, {39, 63, 79, 255});
        char role = ' ';
        Color roleColor = dim;
        if (selectedPass)
        {
            const bool isInput = std::find(selectedPass->Inputs.begin(), selectedPass->Inputs.end(),
                                           resource.Id) != selectedPass->Inputs.end();
            const bool isOutput = std::find(selectedPass->Outputs.begin(), selectedPass->Outputs.end(),
                                            resource.Id) != selectedPass->Outputs.end();
            role = isOutput ? 'O' : (isInput ? 'I' : ' ');
            roleColor = isOutput ? output : (isInput ? input : dim);
        }
        Text(356, resourceY, std::string(1, role), roleColor, 1);
        Text(370, resourceY, (index == m_frameDebugResource ? "> " : "  ") +
                            Shorten(resource.Name, 32), index == m_frameDebugResource ? text : dim, 1);
        Text(655, resourceY, std::to_string(resource.Width) + "X" +
                            std::to_string(resource.Height), dim, 1);
        Text(752, resourceY, Shorten(resource.Format, 15), dim, 1);
        Text(868, resourceY,
             Number(static_cast<double>(resource.EstimatedBytes) / (1024.0 * 1024.0), 1) + "M",
             dim, 1);
        resourceY += 16;
    }

    if (!m_frameDebugSnapshot.Resources.empty())
    {
        const FrameDebugResource& resource = m_frameDebugSnapshot.Resources[m_frameDebugResource];
        Text(356, 208, "MIP " + std::to_string(m_frameDebugMip) + "/" +
                           std::to_string(resource.MipLevels - 1) + "  LAYER " +
                           std::to_string(m_frameDebugLayer) + "/" +
                           std::to_string(resource.Layers - 1) + "  SAMPLES " +
                           std::to_string(resource.Samples), text, 1);
        Text(356, 250, Shorten(resource.Name, 48), heading, 1);
        Text(720, 250, "FROZEN CAPTURE", dim, 1);
    }

    Rectangle(356, 268, 582, 246, {5, 7, 11, 255});
    const FrameDebugResource* selectedResource = m_frameDebugSnapshot.Resources.empty()
        ? nullptr : &m_frameDebugSnapshot.Resources[m_frameDebugResource];
    const bool matchingPreview = selectedResource && m_frameDebugPreview.Valid() &&
        m_frameDebugPreview.ResourceId == selectedResource->Id &&
        m_frameDebugPreview.MipLevel == m_frameDebugMip &&
        m_frameDebugPreview.Layer == m_frameDebugLayer;
    if (matchingPreview)
    {
        const float scale = std::min(582.0f / m_frameDebugPreview.Width,
                                     246.0f / m_frameDebugPreview.Height);
        const int width = std::max(1, static_cast<int>(m_frameDebugPreview.Width * scale));
        const int height = std::max(1, static_cast<int>(m_frameDebugPreview.Height * scale));
        Image(356 + (582 - width) / 2, 268 + (246 - height) / 2,
              width, height, m_frameDebugPreview);
    }
    else if (selectedResource && !selectedResource->Previewable)
        Text(540, 382, "METADATA ONLY / NO PREVIEW", dim, 1);
    else if (!m_frameDebugPreview.Error.empty())
        Text(370, 382, Shorten(m_frameDebugPreview.Error, 76), input, 1);
    else
        Text(580, 382, "CAPTURING...", dim, 1);
}

void DebugOverlay::DrawDetailedPanel(const DebugOverlayMetrics& metrics,
                                      const profiling::CpuProfileSnapshot& cpuProfile,
                                      const profiling::MemoryProfileSnapshot& memoryProfile,
                                      const profiling::GpuProfileSnapshot& gpuProfile)
{

    const Color text{224, 232, 244, 255};
    const Color dim{147, 161, 184, 255};
    const Color accent{65, 220, 170, 255};
    const Color heading{112, 173, 255, 255};
    Rectangle(0, 0, static_cast<int>(DebugOverlayImage::DetailedWidth),
              static_cast<int>(DebugOverlayImage::DetailedHeight), {17, 22, 31, 238});
    Rectangle(0, 0, static_cast<int>(DebugOverlayImage::DetailedWidth), 4, accent);
    Rectangle(0, 0, 1, static_cast<int>(DebugOverlayImage::DetailedHeight), {69, 82, 104, 255});
    Rectangle(static_cast<int>(DebugOverlayImage::DetailedWidth) - 1, 0, 1,
              static_cast<int>(DebugOverlayImage::DetailedHeight), {69, 82, 104, 255});
    Text(14, 12, "RENDER DEBUG", text);
    Text(350, 12, "F3 HIDE", dim, 1);

    const double fps = m_smoothedFrameMilliseconds > 0.001f
        ? 1000.0 / m_smoothedFrameMilliseconds : 0.0;
    Text(14, 38, "API", dim, 1);
    Text(82, 36, Shorten(metrics.BackendName, 30), text);
    Text(14, 56, "VIEW", dim, 1);
    Text(82, 54, std::to_string(metrics.ViewWidth) + " X "
                    + std::to_string(metrics.ViewHeight) + "  FRAME "
                    + std::to_string(metrics.FrameIndex), text);
    Text(14, 74, "FRAME", dim, 1);
    Text(82, 72, Number(m_smoothedFrameMilliseconds) + " MS  "
                    + Number(fps, 1) + " FPS", accent);
    Text(14, 92, "GPU", dim, 1);
    if (metrics.Backend.GpuTimingAvailable)
        Text(82, 90, Number(metrics.Backend.GpuFrameMilliseconds) + " MS  S/M/P "
            + Number(metrics.Backend.GpuShadowMilliseconds, 1) + "/"
            + Number(metrics.Backend.GpuMainMilliseconds, 1) + "/"
            + Number(metrics.Backend.GpuPostMilliseconds, 1), text);
    else
        Text(82, 90, "WARMING UP", dim);
    Text(14, 110, "SCENE", dim, 1);
    Text(82, 108, std::to_string(metrics.ObjectCount) + " OBJECTS  "
                    + std::to_string(metrics.TriangleCount) + " TRIANGLES", text);
    Text(14, 128, "LIGHTS", dim, 1);
    Text(82, 126, "P " + std::to_string(metrics.PointLightCount)
                    + "  S " + std::to_string(metrics.SpotLightCount)
                    + "  A " + std::to_string(metrics.AreaLightCount), text);
    Text(250, 126, "AA " + metrics.AntiAliasing + "  EV "
                     + Number(metrics.Exposure, 2), text);

    if (!gpuProfile.Passes.empty())
    {
        const profiling::GpuPassSummary& slowest = *std::max_element(
            gpuProfile.Passes.begin(), gpuProfile.Passes.end(),
            [](const auto& left, const auto& right)
            { return left.LatestMilliseconds < right.LatestMilliseconds; });
        Text(250, 144, "HOT " + Shorten(slowest.Name, 17) + " " +
                           Number(slowest.LatestMilliseconds, 1) + "MS", heading, 1);
    }

    std::map<std::string, std::map<std::string, std::string>> values;
    {
        std::scoped_lock lock(m_valuesMutex);
        values = m_values;
    }
    int cursorY = 150;
    int customRows = 0;
    for (const auto& [group, groupValues] : values)
    {
        if (customRows >= 1)
            break;
        Text(14, cursorY, Shorten(group, 20), heading, 1);
        cursorY += 14;
        for (const auto& [key, value] : groupValues)
        {
            Text(24, cursorY, Shorten(key + "  " + value, 36), text, 1);
            cursorY += 14;
            if (++customRows >= 1)
                break;
        }
    }

    Text(14, cursorY + 2, "CPU MEMORY", heading, 1);
    cursorY += 16;
    const double memoryMegabytes = static_cast<double>(memoryProfile.CurrentBytes) / (1024.0 * 1024.0);
    const double peakMegabytes = static_cast<double>(memoryProfile.PeakBytes) / (1024.0 * 1024.0);
    Text(24, cursorY, Number(memoryMegabytes, 1) + " MB  PEAK " +
                         Number(peakMegabytes, 1) + " MB  LIVE " +
                         std::to_string(memoryProfile.LiveAllocations), text, 1);
    cursorY += 14;
    if (!memoryProfile.Tags.empty())
    {
        const profiling::MemoryTagStats& topTag = memoryProfile.Tags.front();
        const profiling::MemoryFrameStats* memoryFrame =
            memoryProfile.Frames.empty() ? nullptr : &memoryProfile.Frames.back();
        const double allocatedKilobytes = memoryFrame
            ? static_cast<double>(memoryFrame->AllocatedBytes) / 1024.0 : 0.0;
        const double freedKilobytes = memoryFrame
            ? static_cast<double>(memoryFrame->FreedBytes) / 1024.0 : 0.0;
        Text(24, cursorY, "TOP " + Shorten(topTag.Name, 12) + " " +
                             Number(static_cast<double>(topTag.CurrentBytes) / (1024.0 * 1024.0), 1) +
                             "MB  +" + Number(allocatedKilobytes, 0) + "/-" +
                             Number(freedKilobytes, 0) + "KB", dim, 1);
        cursorY += 14;
    }

    Text(14, cursorY + 2, "CPU ZONES", heading, 1);
    cursorY += 17;
    const int maximumZoneRows = std::max(1, (258 - cursorY) / 14);
    int zoneRows = 0;
    for (const profiling::CpuProfileZoneSummary& summary : cpuProfile.Summaries)
    {
        if (zoneRows >= maximumZoneRows)
            break;
        Text(24, cursorY, Shorten(summary.Name, 23), text, 1);
        Text(330, cursorY, Number(summary.AverageMilliseconds) + " MS", dim, 1);
        cursorY += 14;
        ++zoneRows;
    }
    if (zoneRows == 0)
        Text(24, cursorY, "COLLECTING...", dim, 1);

    Text(14, 266, "FRAME TIME  CPU/GPU  40 MS", dim, 1);
    Graph(14, 278, 452, 32);
}

void DebugOverlay::DrawCpuMemoryMonitor(
    const profiling::MemoryProfileSnapshot& memoryProfile)
{
    BeginRegion(kCpuMonitorX, kCpuMonitorY, kCpuMonitorWidth, kCpuMonitorHeight);
    const Color text{224, 232, 244, 255};
    const Color dim{147, 161, 184, 255};
    const Color accent{65, 220, 170, 255};
    const Color border{69, 82, 104, 255};
    Rectangle(0, 0, static_cast<int>(kCpuMonitorWidth),
              static_cast<int>(kCpuMonitorHeight), {17, 22, 31, 224});
    Rectangle(0, 0, static_cast<int>(kCpuMonitorWidth), 3, accent);
    Rectangle(0, 0, 1, static_cast<int>(kCpuMonitorHeight), border);
    Rectangle(static_cast<int>(kCpuMonitorWidth) - 1, 0, 1,
              static_cast<int>(kCpuMonitorHeight), border);
    Text(10, 10, "CPU MEMORY", accent, 1);
    Text(220, 10, memoryProfile.Enabled ? "LIVE" : "OFF", dim, 1);

    const double megabyte = 1024.0 * 1024.0;
    Text(10, 28, "USED " + Number(static_cast<double>(memoryProfile.CurrentBytes) / megabyte, 1) +
                      " MB  PEAK " + Number(static_cast<double>(memoryProfile.PeakBytes) / megabyte, 1) +
                      " MB", text, 1);
    Text(10, 44, "ALLOCATIONS " + std::to_string(memoryProfile.LiveAllocations), text, 1);
    const profiling::MemoryFrameStats* frame = memoryProfile.Frames.empty()
        ? nullptr : &memoryProfile.Frames.back();
    const double allocatedKilobytes = frame
        ? static_cast<double>(frame->AllocatedBytes) / 1024.0 : 0.0;
    const double freedKilobytes = frame
        ? static_cast<double>(frame->FreedBytes) / 1024.0 : 0.0;
    Text(10, 60, "FRAME +" + Number(allocatedKilobytes, 0) + " / -" +
                      Number(freedKilobytes, 0) + " KB", dim, 1);
    if (!memoryProfile.Tags.empty())
    {
        const profiling::MemoryTagStats& tag = memoryProfile.Tags.front();
        Text(10, 76, "TOP " + Shorten(tag.Name, 18) + "  " +
                          Number(static_cast<double>(tag.CurrentBytes) / megabyte, 1) + " MB",
             dim, 1);
    }
    AddLayer(kCpuMonitorX, kCpuMonitorY, kCpuMonitorWidth, kCpuMonitorHeight,
             m_cpuMemoryPlacement);
}

void DebugOverlay::DrawGpuMonitor(const DebugOverlayMetrics& metrics,
                                  const profiling::GpuProfileSnapshot& gpuProfile)
{
    BeginRegion(kGpuMonitorX, kGpuMonitorY, kGpuMonitorWidth, kGpuMonitorHeight);
    const Color text{224, 232, 244, 255};
    const Color dim{147, 161, 184, 255};
    const Color accent{112, 173, 255, 255};
    const Color good{65, 220, 170, 255};
    const Color border{69, 82, 104, 255};
    Rectangle(0, 0, static_cast<int>(kGpuMonitorWidth),
              static_cast<int>(kGpuMonitorHeight), {17, 22, 31, 224});
    Rectangle(0, 0, static_cast<int>(kGpuMonitorWidth), 3, accent);
    Rectangle(0, 0, 1, static_cast<int>(kGpuMonitorHeight), border);
    Rectangle(static_cast<int>(kGpuMonitorWidth) - 1, 0, 1,
              static_cast<int>(kGpuMonitorHeight), border);
    Text(10, 10, "GPU MONITOR", accent, 1);
    Text(10, 28, Shorten(metrics.Capabilities.AdapterName, 44), text, 1);
    if (gpuProfile.TimingAvailable || metrics.Backend.GpuTimingAvailable)
    {
        const float frameMilliseconds = gpuProfile.TimingAvailable
            ? gpuProfile.FrameMilliseconds : metrics.Backend.GpuFrameMilliseconds;
        const double gpuFps = frameMilliseconds > 0.001f ? 1000.0 / frameMilliseconds : 0.0;
        Text(10, 44, Number(frameMilliseconds, 2) + " MS  " +
                           Number(gpuFps, 0) + " GPU FPS", good, 1);
        if (!gpuProfile.Passes.empty())
        {
            std::string passes;
            for (size_t index = 0; index < std::min<size_t>(gpuProfile.Passes.size(), 3); ++index)
            {
                if (!passes.empty()) passes += "/";
                passes += Number(gpuProfile.Passes[index].LatestMilliseconds, 1);
            }
            Text(10, 60, "PASS MS " + passes, dim, 1);
        }
        else
            Text(10, 60, "SHADOW/MAIN/POST " +
                              Number(metrics.Backend.GpuShadowMilliseconds, 1) + "/" +
                              Number(metrics.Backend.GpuMainMilliseconds, 1) + "/" +
                              Number(metrics.Backend.GpuPostMilliseconds, 1) + " MS", dim, 1);
    }
    else
    {
        Text(10, 44, "GPU TIMING WARMING UP", dim, 1);
        Text(10, 60, "SHADOW/MAIN/POST --/--/--", dim, 1);
    }
    if (gpuProfile.MemoryBudgetAvailable && gpuProfile.Memory.BudgetBytes > 0)
    {
        Text(10, 76, "VRAM " +
                          Number(static_cast<double>(gpuProfile.Memory.UsageBytes) /
                                 (1024.0 * 1024.0), 0) + "/" +
                          Number(static_cast<double>(gpuProfile.Memory.BudgetBytes) /
                                 (1024.0 * 1024.0), 0) + " MB  PEAK " +
                          Number(static_cast<double>(gpuProfile.Memory.PeakUsageBytes) /
                                 (1024.0 * 1024.0), 0), text, 1);
    }
    else if (metrics.Capabilities.DedicatedVideoMemoryBytes > 0)
    {
        Text(10, 76, "DEDICATED VRAM " +
                          std::to_string(metrics.Capabilities.DedicatedVideoMemoryBytes /
                                         (1024ull * 1024ull)) + " MB",
             text, 1);
    }
    else
        Text(10, 76, "DEDICATED VRAM UNKNOWN", dim, 1);
    if (gpuProfile.PipelineStatisticsAvailable)
    {
        Text(10, 92, "DRAWS " + std::to_string(gpuProfile.Pipeline.DrawCalls) +
                          "  PRIMS " + std::to_string(gpuProfile.Pipeline.InputAssemblyPrimitives),
             text, 1);
        Text(10, 108, "VS " + std::to_string(gpuProfile.Pipeline.VertexShaderInvocations) +
                           "  FS " + std::to_string(gpuProfile.Pipeline.FragmentShaderInvocations),
             dim, 1);
    }
    else
    {
        Text(10, 92, "PIPELINE STATISTICS UNAVAILABLE", dim, 1);
        Text(10, 108, "DRAWS/PRIMITIVES --/--", dim, 1);
    }
    Text(10, 124, Shorten(metrics.BackendName, 26) + "  AA " + metrics.AntiAliasing,
         dim, 1);
    AddLayer(kGpuMonitorX, kGpuMonitorY, kGpuMonitorWidth, kGpuMonitorHeight,
             m_gpuPlacement);
}

} // namespace engine::debug
