#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace engine
{

struct MeshInstance;
class Scene;

struct GeometryBatchingStatistics
{
    uint32_t SourceInstances = 0;
    uint32_t EffectiveInstances = 0;
    uint32_t StaticBatches = 0;
    uint32_t DynamicBatches = 0;
    uint32_t StaticBatchedInstances = 0;
    uint32_t DynamicBatchedInstances = 0;
    uint32_t DrawCallsSaved = 0;
    uint32_t CacheHits = 0;
    uint32_t RebuiltBatches = 0;
    uint32_t RejectedBatches = 0;
    uint32_t RecycledBatches = 0;
    uint32_t DeferredToInstancing = 0;
    uint64_t CombinedVertices = 0;
    uint64_t CombinedIndices = 0;
    float BuildMilliseconds = 0.0f;
};

struct SceneBatchBuildResult
{
    std::vector<const MeshInstance*> Instances;
    GeometryBatchingStatistics Statistics;
};

// Backend-neutral geometry batching cache. Combined MeshData objects retain a
// stable CPU identity while Revision informs OpenGL/Vulkan when a dynamic
// batch needs a GPU-buffer refresh.
class SceneBatcher
{
public:
    SceneBatcher();
    ~SceneBatcher();
    SceneBatcher(const SceneBatcher&) = delete;
    SceneBatcher& operator=(const SceneBatcher&) = delete;

    const SceneBatchBuildResult& Build(const Scene& scene, uint64_t frameIndex);
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace engine
