#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::debug {

enum class FrameDebugResourceKind : uint8_t
{
    Texture2D,
    Texture2DArray,
    TextureCube,
    TextureCubeArray,
    Texture3D,
    Renderbuffer
};

enum class FrameDebugVisualization : uint8_t
{
    Color,
    Depth,
    Velocity,
    SingleChannel
};

// Stable across native APIs and process runs, so a selected resource survives
// backend switches and swapchain recreation.
constexpr uint64_t FrameDebugId(std::string_view name)
{
    uint64_t hash = 14695981039346656037ull;
    for (const char character : name)
    {
        hash ^= static_cast<uint8_t>(character);
        hash *= 1099511628211ull;
    }
    return hash;
}

struct FrameDebugResource
{
    uint64_t Id = 0;
    std::string Name;
    FrameDebugResourceKind Kind = FrameDebugResourceKind::Texture2D;
    FrameDebugVisualization Visualization = FrameDebugVisualization::Color;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Layers = 1;
    uint32_t MipLevels = 1;
    uint32_t Samples = 1;
    std::string Format;
    uint64_t EstimatedBytes = 0;
    bool Previewable = true;
    bool Transient = false;
    uint32_t FirstUsePass = UINT32_MAX;
    uint32_t LastUsePass = UINT32_MAX;
    uint32_t AliasSlot = UINT32_MAX;
};

struct FrameDebugPass
{
    std::string Name;
    std::vector<uint64_t> Inputs;
    std::vector<uint64_t> Outputs;
    float GpuMilliseconds = 0.0f;
    bool TimingAvailable = false;
    uint32_t DeclarationIndex = 0;
    uint32_t BarrierCount = 0;
};

struct FrameDebugSnapshot
{
    std::string BackendName;
    uint64_t FrameIndex = 0;
    std::vector<FrameDebugPass> Passes;
    std::vector<FrameDebugResource> Resources;
    uint32_t GraphBarrierCount = 0;
    uint64_t GraphLogicalTransientBytes = 0;
    uint64_t GraphPhysicalTransientBytes = 0;
    uint64_t GraphAliasedBytesSaved = 0;
    float GraphCompileMilliseconds = 0.0f;
};

// Frozen RGBA8 visualization of one native render resource. HDR values are
// display-compressed, depth gets inspection contrast, and velocity is remapped
// around neutral gray by the backend capture implementation.
struct FrameDebugPreview
{
    uint64_t ResourceId = 0;
    uint32_t SourceWidth = 0;
    uint32_t SourceHeight = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t MipLevel = 0;
    uint32_t Layer = 0;
    std::vector<uint8_t> Pixels;
    std::string Error;

    bool Valid() const
    {
        return Width > 0 && Height > 0 &&
               Pixels.size() == static_cast<size_t>(Width) * Height * 4;
    }
};

const FrameDebugResource* FindFrameDebugResource(const FrameDebugSnapshot& snapshot,
                                                 uint64_t id);

} // namespace engine::debug
