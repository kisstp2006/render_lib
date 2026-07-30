#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>

#include "engine/render/Visibility.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/RenderSettings.h"

namespace engine {

class Camera;

struct LodSelection
{
    uint32_t Level = 0;
    float ProjectedDiameterPixels = 0.0f;
    float EstimatedErrorPixels = 0.0f;
};

struct LodStatistics
{
    uint32_t Candidates = 0;
    uint32_t Selected[8]{};
    uint32_t Transitions = 0;
    uint64_t SubmittedTriangles = 0;
    uint64_t Lod0Triangles = 0;
};

// Stateful, backend-neutral LOD choice. History is keyed by the scene's
// stable TemporalId, which makes the hysteresis independent of OpenGL/Vulkan
// command recording and resilient to per-frame command rebuilding.
class LodSelector
{
public:
    LodSelection Select(uint64_t temporalId, uint64_t frameIndex,
                        const AxisAlignedBounds& worldBounds,
                        const Camera& camera, uint32_t viewportHeight,
                        std::span<const MeshLodLevel> levels,
                        const LodSettings& settings,
                        LodStatistics* statistics = nullptr);
    void Reset();

private:
    struct History
    {
        uint32_t Level = 0;
        uint64_t LastSeenFrame = 0;
    };

    std::unordered_map<uint64_t, History> m_history;
};

} // namespace engine
