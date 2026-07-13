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

} // namespace

DebugOverlay::DebugOverlay()
{
    m_image.Pixels.resize(static_cast<size_t>(DebugOverlayImage::Width)
                        * DebugOverlayImage::Height * 4);
    m_frameHistory.reserve(120);
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

void DebugOverlay::Pixel(int x, int y, Color color)
{
    if (x < 0 || y < 0 || x >= static_cast<int>(DebugOverlayImage::Width)
        || y >= static_cast<int>(DebugOverlayImage::Height))
        return;
    const size_t index = (static_cast<size_t>(y) * DebugOverlayImage::Width
                        + static_cast<size_t>(x)) * 4;
    m_image.Pixels[index + 0] = color.R;
    m_image.Pixels[index + 1] = color.G;
    m_image.Pixels[index + 2] = color.B;
    m_image.Pixels[index + 3] = color.A;
}

void DebugOverlay::Rectangle(int x, int y, int width, int height, Color color)
{
    for (int row = std::max(y, 0); row < std::min(y + height, static_cast<int>(DebugOverlayImage::Height)); ++row)
        for (int column = std::max(x, 0); column < std::min(x + width, static_cast<int>(DebugOverlayImage::Width)); ++column)
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
        if (cursor >= static_cast<int>(DebugOverlayImage::Width) - 4)
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
}

void DebugOverlay::Update(const DebugOverlayMetrics& metrics,
                          const profiling::CpuProfileSnapshot& cpuProfile)
{
    if (!m_visible)
        return;

    const float frameMilliseconds = std::clamp(metrics.DeltaSeconds * 1000.0f, 0.0f, 1000.0f);
    if (frameMilliseconds > 0.0f)
        m_smoothedFrameMilliseconds += (frameMilliseconds - m_smoothedFrameMilliseconds) * 0.10f;
    m_frameHistory.push_back(frameMilliseconds);
    if (m_frameHistory.size() > 120)
        m_frameHistory.erase(m_frameHistory.begin());

    const Color text{224, 232, 244, 255};
    const Color dim{147, 161, 184, 255};
    const Color accent{65, 220, 170, 255};
    const Color heading{112, 173, 255, 255};
    Clear({17, 22, 31, 238});
    Rectangle(0, 0, static_cast<int>(DebugOverlayImage::Width), 4, accent);
    Rectangle(0, 0, 1, static_cast<int>(DebugOverlayImage::Height), {69, 82, 104, 255});
    Rectangle(static_cast<int>(DebugOverlayImage::Width) - 1, 0, 1,
              static_cast<int>(DebugOverlayImage::Height), {69, 82, 104, 255});
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

    std::map<std::string, std::map<std::string, std::string>> values;
    {
        std::scoped_lock lock(m_valuesMutex);
        values = m_values;
    }
    int cursorY = 150;
    int customRows = 0;
    for (const auto& [group, groupValues] : values)
    {
        if (customRows >= 3)
            break;
        Text(14, cursorY, Shorten(group, 20), heading, 1);
        cursorY += 14;
        for (const auto& [key, value] : groupValues)
        {
            Text(24, cursorY, Shorten(key + "  " + value, 36), text, 1);
            cursorY += 14;
            if (++customRows >= 3)
                break;
        }
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

    Text(14, 266, "FRAME TIME  40 MS", dim, 1);
    Graph(14, 278, 452, 32);
    ++m_image.Revision;
}

} // namespace engine::debug
