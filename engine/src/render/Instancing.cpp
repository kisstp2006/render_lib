#include "engine/render/Instancing.h"

#include "engine/render/Batching.h"
#include "engine/render/SceneRenderer.h"
#include "engine/scene/Material.h"
#include "engine/scene/RenderSettings.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace engine
{
namespace
{

constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashWord(uint64_t& hash, uint64_t value) noexcept
{
    for (uint32_t shift = 0; shift < 64; shift += 8)
    {
        hash ^= (value >> shift) & 0xffu;
        hash *= kFnvPrime;
    }
}

uint64_t InstanceBatchHash(const PreparedRenderCommand& command) noexcept
{
    const MeshInstance& instance = *command.Source;
    uint64_t hash = MaterialRenderStateHash(instance.Mat);
    HashWord(hash, reinterpret_cast<uintptr_t>(GetCommandMesh(command).get()));
    return hash;
}

bool BatchCompatible(const PreparedRenderCommand& left,
                     const PreparedRenderCommand& right) noexcept
{
    const MeshInstance& a = *left.Source;
    const MeshInstance& b = *right.Source;
    return GetCommandMesh(left).get() == GetCommandMesh(right).get() &&
           MaterialRenderStatesEqual(a.Mat, b.Mat);
}

AxisAlignedBounds UnionBounds(const AxisAlignedBounds& left,
                              const AxisAlignedBounds& right) noexcept
{
    if (!left.Valid) return right;
    if (!right.Valid) return left;
    return {glm::min(left.Minimum, right.Minimum),
            glm::max(left.Maximum, right.Maximum), true};
}

struct HismNode
{
    AxisAlignedBounds Bounds;
    uint32_t First = 0;
    uint32_t Count = 0;
    uint32_t Left = UINT32_MAX;
    uint32_t Right = UINT32_MAX;
    [[nodiscard]] bool Leaf() const noexcept { return Left == UINT32_MAX; }
};

uint32_t BuildHismNode(std::span<const HismCullItem> items,
                       std::vector<uint32_t>& order,
                       std::vector<HismNode>& nodes,
                       uint32_t first, uint32_t count, uint32_t leafSize)
{
    AxisAlignedBounds bounds;
    AxisAlignedBounds centers;
    for (uint32_t offset = 0; offset < count; ++offset)
    {
        const AxisAlignedBounds& itemBounds = items[order[first + offset]].Bounds;
        bounds = UnionBounds(bounds, itemBounds);
        const glm::vec3 center = itemBounds.Center();
        centers = UnionBounds(centers, {center, center, true});
    }
    const uint32_t nodeIndex = static_cast<uint32_t>(nodes.size());
    nodes.push_back({bounds, first, count});
    if (count <= leafSize)
        return nodeIndex;

    const glm::vec3 spread = centers.Maximum - centers.Minimum;
    uint32_t axis = 0;
    if (spread.y > spread.x) axis = 1;
    if (spread.z > spread[axis]) axis = 2;
    const uint32_t middle = first + count / 2;
    std::nth_element(order.begin() + first, order.begin() + middle,
                     order.begin() + first + count,
        [items, axis](uint32_t left, uint32_t right) {
            return items[left].Bounds.Center()[axis] < items[right].Bounds.Center()[axis];
        });
    const uint32_t left = BuildHismNode(items, order, nodes, first,
                                        middle - first, leafSize);
    const uint32_t right = BuildHismNode(items, order, nodes, middle,
                                         first + count - middle, leafSize);
    nodes[nodeIndex].Left = left;
    nodes[nodeIndex].Right = right;
    return nodeIndex;
}

} // namespace

InstanceBatchBuildResult BuildInstanceBatches(
    std::span<const PreparedRenderCommand* const> commands,
    const InstancingSettings& settings, uint32_t firstInstance)
{
    struct CandidateGroup
    {
        const PreparedRenderCommand* Representative = nullptr;
        std::vector<const PreparedRenderCommand*> Commands;
    };

    std::vector<CandidateGroup> groups;
    groups.reserve(commands.size());
    std::unordered_map<uint64_t, std::vector<size_t>> buckets;
    for (const PreparedRenderCommand* command : commands)
    {
        if (!command || !command->Source)
            continue;
        if (!settings.Enabled || !command->Source->AllowInstancing)
        {
            groups.push_back({command, {command}});
            continue;
        }
        const uint64_t hash = InstanceBatchHash(*command);
        size_t groupIndex = SIZE_MAX;
        if (const auto found = buckets.find(hash); found != buckets.end())
        {
            for (size_t candidate : found->second)
            {
                if (BatchCompatible(*groups[candidate].Representative, *command))
                {
                    groupIndex = candidate;
                    break;
                }
            }
        }
        if (groupIndex == SIZE_MAX)
        {
            groupIndex = groups.size();
            groups.push_back({command, {}});
            buckets[hash].push_back(groupIndex);
        }
        groups[groupIndex].Commands.push_back(command);
    }

    InstanceBatchBuildResult result;
    result.Statistics.SourceInstances = static_cast<uint32_t>(commands.size());
    const uint32_t minimumBatchSize = std::max(settings.MinimumBatchSize, 2u);
    for (CandidateGroup& group : groups)
    {
        if (settings.Enabled && group.Representative->Source->AllowInstancing &&
            group.Commands.size() >= minimumBatchSize)
        {
            InstanceDrawBatch batch;
            batch.Representative = group.Representative;
            batch.FirstInstance = firstInstance;
            batch.Commands = std::move(group.Commands);
            firstInstance += static_cast<uint32_t>(batch.Commands.size());
            ++result.Statistics.InstancedBatches;
            result.Statistics.InstancedInstances +=
                static_cast<uint32_t>(batch.Commands.size());
            result.Batches.push_back(std::move(batch));
        }
        else
        {
            for (const PreparedRenderCommand* command : group.Commands)
            {
                result.Batches.push_back({command, {command}, firstInstance++});
            }
        }
    }
    result.Statistics.DrawBatches = static_cast<uint32_t>(result.Batches.size());
    result.Statistics.DrawCallsSaved = result.Statistics.SourceInstances -
                                       result.Statistics.DrawBatches;
    return result;
}

std::vector<VisibilityClassification> CullHierarchicalInstances(
    std::span<const HismCullItem> items, const ViewFrustum& frustum,
    const glm::vec3& cameraPosition, float maxDistance,
    bool frustumCulling, bool distanceCulling, uint32_t leafSize,
    HismCullStatistics* statistics)
{
    std::vector<VisibilityClassification> classifications(
        items.size(), VisibilityClassification::InvalidBounds);
    std::vector<uint32_t> order;
    order.reserve(items.size());
    for (uint32_t index = 0; index < items.size(); ++index)
    {
        if (items[index].Bounds.Valid)
        {
            order.push_back(index);
            classifications[index] = VisibilityClassification::Visible;
        }
    }
    HismCullStatistics local;
    if (order.empty())
    {
        if (statistics) *statistics = local;
        return classifications;
    }

    std::vector<HismNode> nodes;
    nodes.reserve(order.size() * 2);
    const uint32_t root = BuildHismNode(items, order, nodes, 0,
        static_cast<uint32_t>(order.size()), std::max(leafSize, 1u));
    local.NodeCount = static_cast<uint32_t>(nodes.size());
    const float squaredMaxDistance = maxDistance * maxDistance;

    const auto markNode = [&](const HismNode& node,
                              VisibilityClassification classification) {
        for (uint32_t offset = 0; offset < node.Count; ++offset)
            classifications[order[node.First + offset]] = classification;
        ++local.NodesCulled;
        local.InstancesCulled += node.Count;
    };
    const auto visit = [&](auto&& self, uint32_t nodeIndex) -> void {
        const HismNode& node = nodes[nodeIndex];
        ++local.NodesTested;
        if (distanceCulling && maxDistance > 0.0f &&
            SquaredDistanceToBounds(cameraPosition, node.Bounds) > squaredMaxDistance)
        {
            markNode(node, VisibilityClassification::DistanceCulled);
            return;
        }
        if (frustumCulling && !IntersectsFrustum(frustum, node.Bounds))
        {
            markNode(node, VisibilityClassification::FrustumCulled);
            return;
        }
        if (!node.Leaf())
        {
            self(self, node.Left);
            self(self, node.Right);
            return;
        }
        for (uint32_t offset = 0; offset < node.Count; ++offset)
        {
            const uint32_t itemIndex = order[node.First + offset];
            const AxisAlignedBounds& bounds = items[itemIndex].Bounds;
            ++local.LeafTests;
            if (distanceCulling && maxDistance > 0.0f &&
                SquaredDistanceToBounds(cameraPosition, bounds) > squaredMaxDistance)
                classifications[itemIndex] = VisibilityClassification::DistanceCulled;
            else if (frustumCulling && !IntersectsFrustum(frustum, bounds))
                classifications[itemIndex] = VisibilityClassification::FrustumCulled;
        }
    };
    visit(visit, root);
    if (statistics) *statistics = local;
    return classifications;
}

} // namespace engine
