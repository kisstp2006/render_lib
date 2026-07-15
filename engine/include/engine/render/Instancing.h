#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render/Visibility.h"

namespace engine
{

struct InstancingSettings;
struct PreparedRenderCommand;

// std430-compatible transform payload consumed by both native backends.
// PreviousModel keeps TAA motion vectors correct for instanced objects.
struct alignas(16) GpuInstanceData
{
    glm::mat4 Model{1.0f};
    glm::mat4 PreviousModel{1.0f};
};
static_assert(sizeof(GpuInstanceData) == 128);

struct InstanceDrawBatch
{
    const PreparedRenderCommand* Representative = nullptr;
    std::vector<const PreparedRenderCommand*> Commands;
    uint32_t FirstInstance = 0;
};

struct InstanceBatchStatistics
{
    uint32_t SourceInstances = 0;
    uint32_t DrawBatches = 0;
    uint32_t InstancedBatches = 0;
    uint32_t InstancedInstances = 0;
    uint32_t DrawCallsSaved = 0;
};

struct InstanceBatchBuildResult
{
    std::vector<InstanceDrawBatch> Batches;
    InstanceBatchStatistics Statistics;
};

InstanceBatchBuildResult BuildInstanceBatches(
    std::span<const PreparedRenderCommand* const> commands,
    const InstancingSettings& settings,
    uint32_t firstInstance = 0);

struct HismCullItem
{
    uint32_t InstanceIndex = 0;
    AxisAlignedBounds Bounds;
};

struct HismCullStatistics
{
    uint32_t NodeCount = 0;
    uint32_t NodesTested = 0;
    uint32_t NodesCulled = 0;
    uint32_t InstancesCulled = 0;
    uint32_t LeafTests = 0;
};

// Builds a median-split spatial hierarchy and returns one classification per
// input item. Node rejection is conservative; visible leaves receive the same
// individual tests as the non-HISM path.
std::vector<VisibilityClassification> CullHierarchicalInstances(
    std::span<const HismCullItem> items,
    const ViewFrustum& frustum,
    const glm::vec3& cameraPosition,
    float maxDistance,
    bool frustumCulling,
    bool distanceCulling,
    uint32_t leafSize,
    HismCullStatistics* statistics = nullptr);

} // namespace engine
