#include "engine/asset/cook/MeshProcessing.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define ENABLE_VHACD_IMPLEMENTATION 1
#include <VHACD.h>
#include <meshoptimizer.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>

namespace engine::assets::cook
{
namespace
{

struct TriangleReference
{
    std::array<uint32_t, 3> Indices{};
    glm::vec3 Minimum{0.0f};
    glm::vec3 Maximum{0.0f};
    glm::vec3 Centroid{0.0f};
};

uint32_t BuildBvhRecursive(std::vector<TriangleReference> &triangles, uint32_t first, uint32_t count,
                           std::vector<CollisionBvhNode> &nodes)
{
    const uint32_t nodeIndex = static_cast<uint32_t>(nodes.size());
    nodes.emplace_back();
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    glm::vec3 centroidMinimum(std::numeric_limits<float>::max());
    glm::vec3 centroidMaximum(std::numeric_limits<float>::lowest());
    for (uint32_t index = first; index < first + count; ++index)
    {
        minimum = glm::min(minimum, triangles[index].Minimum);
        maximum = glm::max(maximum, triangles[index].Maximum);
        centroidMinimum = glm::min(centroidMinimum, triangles[index].Centroid);
        centroidMaximum = glm::max(centroidMaximum, triangles[index].Centroid);
    }
    nodes[nodeIndex].BoundsMinimum = minimum;
    nodes[nodeIndex].BoundsMaximum = maximum;
    if (count <= 8u)
    {
        nodes[nodeIndex].FirstTriangle = first;
        nodes[nodeIndex].TriangleCount = count;
        return nodeIndex;
    }

    const glm::vec3 extent = centroidMaximum - centroidMinimum;
    uint32_t axis = extent.y > extent.x ? 1u : 0u;
    if (extent.z > extent[axis])
        axis = 2u;
    std::stable_sort(triangles.begin() + first, triangles.begin() + first + count,
                     [axis](const TriangleReference &left, const TriangleReference &right) {
                         if (left.Centroid[axis] != right.Centroid[axis])
                             return left.Centroid[axis] < right.Centroid[axis];
                         return left.Indices < right.Indices;
                     });
    const uint32_t leftCount = count / 2u;
    const uint32_t left = BuildBvhRecursive(triangles, first, leftCount, nodes);
    const uint32_t right = BuildBvhRecursive(triangles, first + leftCount, count - leftCount, nodes);
    nodes[nodeIndex].LeftChild = left;
    nodes[nodeIndex].RightChild = right;
    return nodeIndex;
}

bool BuildBvh(CollisionMesh &mesh, std::string *error)
{
    if (mesh.Indices.empty() || mesh.Indices.size() % 3u != 0)
    {
        if (error)
            *error = "Collision BVH requires indexed triangle geometry";
        return false;
    }
    std::vector<TriangleReference> triangles(mesh.Indices.size() / 3u);
    for (size_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex)
    {
        TriangleReference &triangle = triangles[triangleIndex];
        for (size_t corner = 0; corner < 3; ++corner)
        {
            const uint32_t index = mesh.Indices[triangleIndex * 3u + corner];
            if (index >= mesh.Vertices.size())
            {
                if (error)
                    *error = "Collision mesh contains an out-of-range vertex index";
                return false;
            }
            triangle.Indices[corner] = index;
        }
        const glm::vec3 a = mesh.Vertices[triangle.Indices[0]];
        const glm::vec3 b = mesh.Vertices[triangle.Indices[1]];
        const glm::vec3 c = mesh.Vertices[triangle.Indices[2]];
        triangle.Minimum = glm::min(a, glm::min(b, c));
        triangle.Maximum = glm::max(a, glm::max(b, c));
        triangle.Centroid = (a + b + c) / 3.0f;
    }
    mesh.Bvh.clear();
    mesh.Bvh.reserve(triangles.size() * 2u);
    BuildBvhRecursive(triangles, 0, static_cast<uint32_t>(triangles.size()), mesh.Bvh);
    mesh.Indices.clear();
    mesh.Indices.reserve(triangles.size() * 3u);
    for (const TriangleReference &triangle : triangles)
        mesh.Indices.insert(mesh.Indices.end(), triangle.Indices.begin(), triangle.Indices.end());
    return true;
}

bool MergeSources(std::span<const CollisionSourceMesh> sources, MeshData &merged, std::string *error)
{
    for (const CollisionSourceMesh &source : sources)
    {
        if (!source.Mesh || source.Mesh->Vertices.empty() || source.Mesh->Indices.empty())
            continue;
        const uint32_t baseVertex = static_cast<uint32_t>(merged.Vertices.size());
        merged.Vertices.reserve(merged.Vertices.size() + source.Mesh->Vertices.size());
        for (const Vertex &input : source.Mesh->Vertices)
        {
            Vertex vertex = input;
            vertex.Position = glm::vec3(source.Transform * glm::vec4(input.Position, 1.0f));
            merged.Vertices.push_back(vertex);
        }
        merged.Indices.reserve(merged.Indices.size() + source.Mesh->Indices.size());
        for (uint32_t index : source.Mesh->Indices)
        {
            if (index >= source.Mesh->Vertices.size() || baseVertex > UINT32_MAX - index)
            {
                if (error)
                    *error = "Collision source contains invalid or excessive vertex indices";
                return false;
            }
            merged.Indices.push_back(baseVertex + index);
        }
    }
    if (merged.Vertices.empty() || merged.Indices.empty())
    {
        if (error)
            *error = "No triangle geometry was available for collision cooking";
        return false;
    }
    // Render vertices split at hard normals/UV seams still represent the same
    // collision point. Reindex by position before the general cache/fetch pass
    // so the triangle mesh is watertight for BVH and convex decomposition.
    std::vector<uint32_t> positionRemap(merged.Vertices.size());
    meshopt_generatePositionRemap(positionRemap.data(), &merged.Vertices.front().Position.x,
                                  merged.Vertices.size(), sizeof(Vertex));
    for (uint32_t &index : merged.Indices)
        index = positionRemap[index];
    return OptimizeMesh(merged, false, nullptr, error);
}

CollisionMesh ToCollisionMesh(const MeshData &mesh, bool convex)
{
    CollisionMesh result;
    result.Convex = convex;
    result.Vertices.reserve(mesh.Vertices.size());
    for (const Vertex &vertex : mesh.Vertices)
        result.Vertices.push_back(vertex.Position);
    result.Indices = mesh.Indices;
    return result;
}

bool CookConvexDecomposition(const MeshData &mesh, const CollisionCookSettings &settings,
                             CollisionData &output, std::string *error)
{
    std::vector<float> points(mesh.Vertices.size() * 3u);
    for (size_t index = 0; index < mesh.Vertices.size(); ++index)
    {
        points[index * 3u + 0u] = mesh.Vertices[index].Position.x;
        points[index * 3u + 1u] = mesh.Vertices[index].Position.y;
        points[index * 3u + 2u] = mesh.Vertices[index].Position.z;
    }
    struct Releaser
    {
        void operator()(VHACD::IVHACD *value) const
        {
            if (value)
                value->Release();
        }
    };
    std::unique_ptr<VHACD::IVHACD, Releaser> vhacd(VHACD::CreateVHACD());
    if (!vhacd)
    {
        if (error)
            *error = "V-HACD collision cooker initialization failed";
        return false;
    }
    VHACD::IVHACD::Parameters parameters;
    parameters.m_maxConvexHulls = settings.MaxConvexHulls;
    parameters.m_resolution = settings.VoxelResolution;
    parameters.m_minimumVolumePercentErrorAllowed = settings.ConvexErrorPercent;
    parameters.m_maxNumVerticesPerCH = settings.MaxVerticesPerHull;
    parameters.m_asyncACD = false;
    parameters.m_shrinkWrap = true;
    if (!vhacd->Compute(points.data(), static_cast<uint32_t>(mesh.Vertices.size()), mesh.Indices.data(),
                        static_cast<uint32_t>(mesh.Indices.size() / 3u), parameters))
    {
        if (error)
            *error = "V-HACD rejected the collision source mesh";
        return false;
    }
    const uint32_t hullCount = vhacd->GetNConvexHulls();
    if (hullCount == 0)
    {
        if (error)
            *error = "V-HACD produced no convex hulls";
        return false;
    }
    output.Meshes.reserve(hullCount);
    for (uint32_t hullIndex = 0; hullIndex < hullCount; ++hullIndex)
    {
        VHACD::IVHACD::ConvexHull hull;
        if (!vhacd->GetConvexHull(hullIndex, hull) || hull.m_points.empty() || hull.m_triangles.empty())
            continue;
        CollisionMesh cooked;
        cooked.Convex = true;
        cooked.Vertices.reserve(hull.m_points.size());
        for (const VHACD::Vertex &point : hull.m_points)
            cooked.Vertices.emplace_back(static_cast<float>(point.mX), static_cast<float>(point.mY),
                                         static_cast<float>(point.mZ));
        cooked.Indices.reserve(hull.m_triangles.size() * 3u);
        for (const VHACD::Triangle &triangle : hull.m_triangles)
        {
            cooked.Indices.push_back(triangle.mI0);
            cooked.Indices.push_back(triangle.mI1);
            cooked.Indices.push_back(triangle.mI2);
        }
        if (!BuildBvh(cooked, error))
            return false;
        output.Meshes.push_back(std::move(cooked));
    }
    if (output.Meshes.empty())
    {
        if (error)
            *error = "V-HACD produced only empty convex hulls";
        return false;
    }
    return true;
}

} // namespace

bool ValidateCollisionCookSettings(const CollisionCookSettings &settings, std::string *error)
{
    const bool validRatio = settings.TriangleRatio > 0.0f && settings.TriangleRatio <= 1.0f;
    const bool validError = settings.SimplificationError >= 0.0f && settings.SimplificationError <= 1.0f;
    const bool validHulls = settings.MaxConvexHulls >= 1u && settings.MaxConvexHulls <= 256u;
    const bool validVertices = settings.MaxVerticesPerHull >= 4u && settings.MaxVerticesPerHull <= 1024u;
    const bool validResolution = settings.VoxelResolution >= 10000u && settings.VoxelResolution <= 10000000u;
    const bool validConvexError = settings.ConvexErrorPercent >= 0.01f && settings.ConvexErrorPercent <= 100.0f;
    if (validRatio && validError && validHulls && validVertices && validResolution && validConvexError)
        return true;
    if (error)
        *error = "Collision cooking settings are outside their supported range";
    return false;
}

bool CookCollision(std::span<const CollisionSourceMesh> sources, const CollisionCookSettings &settings,
                   CollisionData &output, std::string *error)
{
    output = {};
    output.Type = settings.Type;
    if (settings.Type == CollisionType::None)
        return true;
    if (!ValidateCollisionCookSettings(settings, error))
        return false;

    MeshData merged;
    if (!MergeSources(sources, merged, error))
        return false;
    if (settings.Type == CollisionType::ConvexDecomposition)
        return CookConvexDecomposition(merged, settings, output, error);

    if (settings.Type == CollisionType::SimplifiedTriangleMesh && settings.TriangleRatio < 1.0f)
    {
        std::vector<MeshLod> lods;
        if (!GenerateMeshLods(merged, 2, settings.TriangleRatio, settings.SimplificationError, true, lods, error))
            return false;
        if (lods.size() > 1)
            merged = std::move(lods.back().Mesh);
    }
    CollisionMesh cooked = ToCollisionMesh(merged, false);
    if (!BuildBvh(cooked, error))
        return false;
    output.Meshes.push_back(std::move(cooked));
    return true;
}

} // namespace engine::assets::cook
