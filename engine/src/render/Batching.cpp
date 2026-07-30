#include "engine/render/Batching.h"

#include "engine/render/Visibility.h"
#include "engine/scene/Material.h"
#include "engine/scene/MeshCombiner.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <unordered_map>

namespace engine
{
namespace
{

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashWord(uint64_t& hash, uint64_t value) noexcept
{
    for (uint32_t shift = 0; shift < 64; shift += 8)
    {
        hash ^= (value >> shift) & 0xffu;
        hash *= kFnvPrime;
    }
}

void HashFloat(uint64_t& hash, float value) noexcept
{
    HashWord(hash, std::bit_cast<uint32_t>(value));
}

bool EqualFloat(float left, float right) noexcept
{
    return std::bit_cast<uint32_t>(left) == std::bit_cast<uint32_t>(right);
}

bool EqualMatrix(const glm::mat4& left, const glm::mat4& right) noexcept
{
    for (uint32_t column = 0; column < 4; ++column)
        for (uint32_t row = 0; row < 4; ++row)
            if (!EqualFloat(left[column][row], right[column][row]))
                return false;
    return true;
}

void HashMatrix(uint64_t& hash, const glm::mat4& value) noexcept
{
    for (uint32_t column = 0; column < 4; ++column)
        for (uint32_t row = 0; row < 4; ++row)
            HashFloat(hash, value[column][row]);
}

enum class BatchKind : uint8_t
{
    Static,
    Dynamic
};

struct Candidate
{
    const MeshInstance* Instance = nullptr;
    size_t SourceIndex = 0;
    uint64_t StableId = 0;
};

struct CandidateGroup
{
    BatchKind Kind = BatchKind::Static;
    Material Mat;
    int32_t CellX = 0;
    int32_t CellY = 0;
    int32_t CellZ = 0;
    uint64_t ExplicitGroup = 0;
    uint32_t MaxDistanceBits = 0;
    bool CastsShadows = true;
    bool AlwaysVisible = false;
    uint64_t CacheBaseKey = 0;
    std::vector<Candidate> Members;
};

bool GroupCompatible(const CandidateGroup& group, BatchKind kind,
                     const MeshInstance& instance, int32_t cellX,
                     int32_t cellY, int32_t cellZ) noexcept
{
    return group.Kind == kind && group.CellX == cellX && group.CellY == cellY &&
           group.CellZ == cellZ && group.ExplicitGroup == instance.BatchGroupId &&
           group.CastsShadows == instance.CastsShadows &&
           group.AlwaysVisible == instance.AlwaysVisible &&
           group.MaxDistanceBits == std::bit_cast<uint32_t>(instance.MaxDrawDistance) &&
           MaterialRenderStatesEqual(group.Mat, instance.Mat);
}

uint64_t StableInstanceId(const MeshInstance& instance, size_t index) noexcept
{
    return instance.TemporalId != 0
        ? instance.TemporalId
        : (0x2000000000000000ull ^ static_cast<uint64_t>(index + 1u));
}

} // namespace

struct SceneBatcher::Impl
{
    struct TransformHistory
    {
        glm::mat4 Transform{1.0f};
        uint32_t StableFrames = 0;
        uint64_t LastSeenFrame = 0;
        bool Seen = false;
    };

    struct BoundsCache
    {
        uint64_t Revision = 0;
        const void* VertexData = nullptr;
        size_t VertexCount = 0;
        AxisAlignedBounds Bounds;
    };

    struct CachedBatch
    {
        uint64_t LogicalKey = 0;
        uint64_t ContentSignature = 0;
        uint64_t LastUsedFrame = 0;
        BatchKind Kind = BatchKind::Static;
        Material Mat;
        std::shared_ptr<MeshData> Mesh;
        MeshInstance Instance;
    };

    const Scene* SceneIdentity = nullptr;
    std::unordered_map<uint64_t, TransformHistory> TransformHistories;
    std::unordered_map<const MeshData*, BoundsCache> Bounds;
    std::deque<CachedBatch> Cache;
    std::unordered_map<uint64_t, std::vector<size_t>> CacheBuckets;
    SceneBatchBuildResult Result;

    void Reset()
    {
        SceneIdentity = nullptr;
        TransformHistories.clear();
        Bounds.clear();
        Cache.clear();
        CacheBuckets.clear();
        Result = {};
    }

    AxisAlignedBounds LocalBounds(const MeshData& mesh)
    {
        BoundsCache& cached = Bounds[&mesh];
        if (cached.Revision != mesh.Revision ||
            cached.VertexData != mesh.Vertices.data() ||
            cached.VertexCount != mesh.Vertices.size() || !cached.Bounds.Valid)
        {
            cached.Revision = mesh.Revision;
            cached.VertexData = mesh.Vertices.data();
            cached.VertexCount = mesh.Vertices.size();
            cached.Bounds = ComputeMeshBounds(mesh);
        }
        return cached.Bounds;
    }

    size_t FindOrCreateCache(uint64_t logicalKey, BatchKind kind,
                             const Material& material, uint64_t frameIndex,
                             uint32_t maximumCachedBatches)
    {
        if (const auto found = CacheBuckets.find(logicalKey);
            found != CacheBuckets.end())
        {
            for (size_t index : found->second)
            {
                CachedBatch& cached = Cache[index];
                if (cached.Kind == kind &&
                    MaterialRenderStatesEqual(cached.Mat, material))
                    return index;
            }
        }

        const size_t maximum = std::max<size_t>(maximumCachedBatches, 1u);
        if (Cache.size() >= maximum)
        {
            size_t oldestIndex = SIZE_MAX;
            uint64_t oldestFrame = UINT64_MAX;
            for (size_t index = 0; index < Cache.size(); ++index)
            {
                if (Cache[index].LastUsedFrame != frameIndex &&
                    Cache[index].LastUsedFrame < oldestFrame)
                {
                    oldestFrame = Cache[index].LastUsedFrame;
                    oldestIndex = index;
                }
            }
            if (oldestIndex != SIZE_MAX)
            {
                CachedBatch& cached = Cache[oldestIndex];
                auto& oldBucket = CacheBuckets[cached.LogicalKey];
                std::erase(oldBucket, oldestIndex);
                if (oldBucket.empty())
                    CacheBuckets.erase(cached.LogicalKey);
                cached.LogicalKey = logicalKey;
                cached.ContentSignature = 0;
                cached.Kind = kind;
                cached.Mat = material;
                CacheBuckets[logicalKey].push_back(oldestIndex);
                ++Result.Statistics.RecycledBatches;
                return oldestIndex;
            }
        }
        CachedBatch cached;
        cached.LogicalKey = logicalKey;
        cached.Kind = kind;
        cached.Mat = material;
        cached.Mesh = std::make_shared<MeshData>();
        Cache.push_back(std::move(cached));
        const size_t index = Cache.size() - 1u;
        CacheBuckets[logicalKey].push_back(index);
        return index;
    }

    bool FinalizeChunk(const CandidateGroup& group,
                       std::span<const Candidate> members,
                       uint32_t chunkIndex, uint32_t minimumSize,
                       uint32_t maximumCachedBatches,
                       uint64_t frameIndex, std::vector<uint8_t>& consumed,
                       std::vector<size_t>& activeCaches)
    {
        if (members.size() < std::max(minimumSize, 2u))
            return false;
        uint64_t logicalKey = group.CacheBaseKey;
        HashWord(logicalKey, chunkIndex);
        uint64_t content = kFnvOffset;
        std::vector<MeshCombineSource> sources;
        sources.reserve(members.size());
        for (const Candidate& candidate : members)
        {
            HashWord(logicalKey, candidate.StableId);
            HashWord(content, candidate.StableId);
            HashWord(content, reinterpret_cast<uintptr_t>(candidate.Instance->Mesh.get()));
            HashWord(content, candidate.Instance->Mesh->Revision);
            HashMatrix(content, candidate.Instance->Transform);
            sources.push_back({candidate.Instance->Mesh.get(),
                               candidate.Instance->Transform});
        }
        const size_t cacheIndex = FindOrCreateCache(
            logicalKey, group.Kind, group.Mat, frameIndex,
            maximumCachedBatches);
        CachedBatch& cached = Cache[cacheIndex];

        if (cached.ContentSignature == content &&
            !cached.Mesh->Vertices.empty() && !cached.Mesh->Indices.empty())
        {
            ++Result.Statistics.CacheHits;
        }
        else
        {
            MeshData combined;
            std::string error;
            if (!CombineMeshes(sources, combined, nullptr, &error))
            {
                ++Result.Statistics.RejectedBatches;
                return false;
            }
            combined.RuntimeMutable = group.Kind == BatchKind::Dynamic;
            combined.Revision = cached.Mesh->Revision + 1u;
            *cached.Mesh = std::move(combined);
            cached.ContentSignature = content;
            ++Result.Statistics.RebuiltBatches;
        }

        cached.LastUsedFrame = frameIndex;
        cached.Mat = group.Mat;
        cached.Instance = {};
        cached.Instance.TemporalId = 0x4000000000000000ull ^ logicalKey ^ content;
        cached.Instance.Mesh = cached.Mesh;
        cached.Instance.Mat = group.Mat;
        cached.Instance.Transform = glm::mat4(1.0f);
        cached.Instance.CastsShadows = group.CastsShadows;
        cached.Instance.AlwaysVisible = group.AlwaysVisible;
        cached.Instance.AllowInstancing = false;
        cached.Instance.AllowBatching = false;
        cached.Instance.Mobility = MeshMobility::Static;
        cached.Instance.BatchGroupId = group.ExplicitGroup;
        cached.Instance.MaxDrawDistance = std::bit_cast<float>(group.MaxDistanceBits);
        for (const Candidate& candidate : members)
            consumed[candidate.SourceIndex] = 1;
        activeCaches.push_back(cacheIndex);

        const uint32_t memberCount = static_cast<uint32_t>(members.size());
        if (group.Kind == BatchKind::Static)
        {
            ++Result.Statistics.StaticBatches;
            Result.Statistics.StaticBatchedInstances += memberCount;
        }
        else
        {
            ++Result.Statistics.DynamicBatches;
            Result.Statistics.DynamicBatchedInstances += memberCount;
        }
        Result.Statistics.CombinedVertices += cached.Mesh->Vertices.size();
        Result.Statistics.CombinedIndices += cached.Mesh->Indices.size();
        return true;
    }

    const SceneBatchBuildResult& Build(const Scene& scene, uint64_t frameIndex)
    {
        const auto start = std::chrono::steady_clock::now();
        if (SceneIdentity != &scene)
        {
            Reset();
            SceneIdentity = &scene;
        }
        Result = {};
        const auto& source = scene.Instances();
        Result.Statistics.SourceInstances = static_cast<uint32_t>(source.size());
        if (!scene.Batching.Enabled || source.size() < 2)
        {
            Result.Instances.reserve(source.size());
            for (const MeshInstance& instance : source)
                Result.Instances.push_back(&instance);
            Result.Statistics.EffectiveInstances =
                static_cast<uint32_t>(Result.Instances.size());
            Result.Statistics.BuildMilliseconds =
                std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
            return Result;
        }

        const GeometryBatchingSettings& settings = scene.Batching;
        const bool preserveMotion = settings.PreserveMotionVectors &&
            scene.PostProcess.AntiAliasing == AntiAliasingMode::Taa;
        std::vector<CandidateGroup> groups;
        std::unordered_map<uint64_t, std::vector<size_t>> buckets;
        std::vector<uint8_t> consumed(source.size(), 0);

        for (size_t index = 0; index < source.size(); ++index)
        {
            const MeshInstance& instance = source[index];
            // A spatially combined mesh cannot change level independently.
            // Keep authored/generated LOD chains on the per-instance path;
            // they still profit from later exact LOD-aware GPU instancing.
            if (!instance.AllowBatching || !instance.LodLevels.empty() || !instance.Mesh ||
                instance.Mesh->Vertices.empty() || instance.Mesh->Indices.empty())
                continue;

            const uint64_t stableId = StableInstanceId(instance, index);
            TransformHistory& history = TransformHistories[stableId];
            const bool unchanged = history.Seen &&
                EqualMatrix(history.Transform, instance.Transform);
            history.StableFrames = unchanged ? history.StableFrames + 1u : 0u;
            history.Transform = instance.Transform;
            history.LastSeenFrame = frameIndex;
            history.Seen = true;

            BatchKind kind = BatchKind::Static;
            if (instance.Mobility == MeshMobility::Static)
            {
                if (!settings.StaticBatching)
                    continue;
            }
            else
            {
                kind = BatchKind::Dynamic;
                if (!settings.DynamicBatching ||
                    instance.Mesh->Vertices.size() > settings.DynamicMaxSourceVertices ||
                    (preserveMotion &&
                     history.StableFrames < settings.DynamicStabilityFrames))
                    continue;
            }

            const AxisAlignedBounds worldBounds = TransformBounds(
                LocalBounds(*instance.Mesh), instance.Transform);
            if (!worldBounds.Valid)
                continue;
            int32_t cellX = 0, cellY = 0, cellZ = 0;
            if (settings.SpatialCellSize > 0.0f)
            {
                const glm::vec3 cell = glm::floor(
                    worldBounds.Center() / settings.SpatialCellSize);
                cellX = static_cast<int32_t>(cell.x);
                cellY = static_cast<int32_t>(cell.y);
                cellZ = static_cast<int32_t>(cell.z);
            }

            uint64_t cacheBaseKey = MaterialRenderStateHash(instance.Mat);
            HashWord(cacheBaseKey, static_cast<uint64_t>(kind));
            HashWord(cacheBaseKey, instance.BatchGroupId);
            HashWord(cacheBaseKey, instance.CastsShadows ? 1u : 0u);
            HashWord(cacheBaseKey, instance.AlwaysVisible ? 1u : 0u);
            HashWord(cacheBaseKey, std::bit_cast<uint32_t>(instance.MaxDrawDistance));
            uint64_t baseKey = cacheBaseKey;
            HashWord(baseKey, static_cast<uint32_t>(cellX));
            HashWord(baseKey, static_cast<uint32_t>(cellY));
            HashWord(baseKey, static_cast<uint32_t>(cellZ));

            size_t groupIndex = SIZE_MAX;
            if (const auto found = buckets.find(baseKey); found != buckets.end())
            {
                for (size_t candidate : found->second)
                {
                    if (GroupCompatible(groups[candidate], kind, instance,
                                        cellX, cellY, cellZ))
                    {
                        groupIndex = candidate;
                        break;
                    }
                }
            }
            if (groupIndex == SIZE_MAX)
            {
                groupIndex = groups.size();
                CandidateGroup group;
                group.Kind = kind;
                group.Mat = instance.Mat;
                group.CellX = cellX;
                group.CellY = cellY;
                group.CellZ = cellZ;
                group.ExplicitGroup = instance.BatchGroupId;
                group.MaxDistanceBits = std::bit_cast<uint32_t>(instance.MaxDrawDistance);
                group.CastsShadows = instance.CastsShadows;
                group.AlwaysVisible = instance.AlwaysVisible;
                group.CacheBaseKey = cacheBaseKey;
                groups.push_back(std::move(group));
                buckets[baseKey].push_back(groupIndex);
            }
            groups[groupIndex].Members.push_back({&instance, index, stableId});
        }

        std::vector<size_t> activeCaches;
        const uint64_t maximumVertices = std::max<uint64_t>(
            settings.MaximumVerticesPerBatch, 3u);
        const uint64_t maximumIndices = std::max<uint64_t>(
            settings.MaximumIndicesPerBatch, 3u);
        for (const CandidateGroup& group : groups)
        {
            std::vector<Candidate> batchMembers;
            if (settings.PreferInstancingForRepeatedMeshes)
            {
                std::unordered_map<const MeshData*, uint32_t> meshCounts;
                for (const Candidate& candidate : group.Members)
                    if (candidate.Instance->AllowInstancing)
                        ++meshCounts[candidate.Instance->Mesh.get()];
                const uint32_t instancingMinimum = std::max(
                    scene.Instancing.MinimumBatchSize, 2u);
                batchMembers.reserve(group.Members.size());
                for (const Candidate& candidate : group.Members)
                {
                    const auto found = meshCounts.find(candidate.Instance->Mesh.get());
                    if (candidate.Instance->AllowInstancing && found != meshCounts.end() &&
                        found->second >= instancingMinimum)
                    {
                        ++Result.Statistics.DeferredToInstancing;
                        continue;
                    }
                    batchMembers.push_back(candidate);
                }
            }
            else
                batchMembers = group.Members;

            size_t first = 0;
            uint64_t vertices = 0;
            uint64_t indices = 0;
            uint32_t chunkIndex = 0;
            const uint32_t minimum = group.Kind == BatchKind::Static
                ? settings.MinimumStaticBatchSize
                : settings.MinimumDynamicBatchSize;
            for (size_t member = 0; member < batchMembers.size(); ++member)
            {
                const MeshData& mesh = *batchMembers[member].Instance->Mesh;
                const bool overflow = member > first &&
                    (vertices + mesh.Vertices.size() > maximumVertices ||
                     indices + mesh.Indices.size() > maximumIndices);
                if (overflow)
                {
                    FinalizeChunk(group,
                        std::span(batchMembers).subspan(first, member - first),
                        chunkIndex++, minimum, settings.MaximumCachedBatches,
                        frameIndex, consumed, activeCaches);
                    first = member;
                    vertices = 0;
                    indices = 0;
                }
                vertices += mesh.Vertices.size();
                indices += mesh.Indices.size();
            }
            if (first < batchMembers.size())
                FinalizeChunk(group, std::span(batchMembers).subspan(first),
                              chunkIndex, minimum, settings.MaximumCachedBatches,
                              frameIndex, consumed,
                              activeCaches);
        }

        Result.Instances.reserve(source.size());
        for (size_t index = 0; index < source.size(); ++index)
            if (consumed[index] == 0)
                Result.Instances.push_back(&source[index]);
        for (size_t cacheIndex : activeCaches)
            Result.Instances.push_back(&Cache[cacheIndex].Instance);

        const uint32_t batched = Result.Statistics.StaticBatchedInstances +
                                 Result.Statistics.DynamicBatchedInstances;
        const uint32_t batches = Result.Statistics.StaticBatches +
                                 Result.Statistics.DynamicBatches;
        Result.Statistics.DrawCallsSaved = batched >= batches
            ? batched - batches : 0u;
        Result.Statistics.EffectiveInstances =
            static_cast<uint32_t>(Result.Instances.size());
        Result.Statistics.BuildMilliseconds =
            std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - start).count();

        if ((frameIndex % 240u) == 0u)
        {
            std::erase_if(TransformHistories, [frameIndex](const auto& entry) {
                return frameIndex > entry.second.LastSeenFrame + 240u;
            });
        }
        return Result;
    }
};

SceneBatcher::SceneBatcher() : m_impl(std::make_unique<Impl>()) {}
SceneBatcher::~SceneBatcher() = default;

const SceneBatchBuildResult& SceneBatcher::Build(const Scene& scene,
                                                  uint64_t frameIndex)
{
    return m_impl->Build(scene, frameIndex);
}

void SceneBatcher::Reset()
{
    m_impl->Reset();
}

} // namespace engine
