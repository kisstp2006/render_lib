#include "engine/asset/cook/MeshProcessing.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace engine::assets::cook
{
namespace
{

bool ValidateMesh(const MeshData &mesh, std::string *error)
{
    if (mesh.Vertices.empty() || mesh.Indices.empty() || mesh.Indices.size() % 3u != 0)
    {
        if (error)
            *error = "Mesh optimization requires non-empty indexed triangle geometry";
        return false;
    }
    for (uint32_t index : mesh.Indices)
    {
        if (index >= mesh.Vertices.size())
        {
            if (error)
                *error = "Mesh optimization found an out-of-range vertex index";
            return false;
        }
    }
    return true;
}

void CompactVertices(MeshData &mesh)
{
    std::vector<Vertex> vertices(mesh.Vertices.size());
    const size_t vertexCount = meshopt_optimizeVertexFetch(vertices.data(), mesh.Indices.data(), mesh.Indices.size(),
                                                           mesh.Vertices.data(), mesh.Vertices.size(), sizeof(Vertex));
    vertices.resize(vertexCount);
    mesh.Vertices = std::move(vertices);
}

} // namespace

bool OptimizeMesh(MeshData &mesh, bool weldVertices, MeshOptimizationReport *report, std::string *error)
{
    if (!ValidateMesh(mesh, error))
        return false;

    MeshOptimizationReport local;
    local.InputVertices = mesh.Vertices.size();
    local.Triangles = mesh.Indices.size() / 3u;
    const auto cacheBefore = meshopt_analyzeVertexCache(mesh.Indices.data(), mesh.Indices.size(), mesh.Vertices.size(),
                                                        16, 32, 0);
    const auto fetchBefore = meshopt_analyzeVertexFetch(mesh.Indices.data(), mesh.Indices.size(), mesh.Vertices.size(),
                                                        sizeof(Vertex));
    local.VertexCacheAcmrBefore = cacheBefore.acmr;
    local.VertexFetchOverfetchBefore = fetchBefore.overfetch;

    if (weldVertices)
    {
        std::vector<uint32_t> remap(mesh.Vertices.size());
        const size_t uniqueVertices = meshopt_generateVertexRemap(
            remap.data(), mesh.Indices.data(), mesh.Indices.size(), mesh.Vertices.data(), mesh.Vertices.size(),
            sizeof(Vertex));
        std::vector<uint32_t> indices(mesh.Indices.size());
        std::vector<Vertex> vertices(uniqueVertices);
        meshopt_remapIndexBuffer(indices.data(), mesh.Indices.data(), mesh.Indices.size(), remap.data());
        meshopt_remapVertexBuffer(vertices.data(), mesh.Vertices.data(), mesh.Vertices.size(), sizeof(Vertex),
                                  remap.data());
        mesh.Indices = std::move(indices);
        mesh.Vertices = std::move(vertices);
    }

    std::vector<uint32_t> optimized(mesh.Indices.size());
    meshopt_optimizeVertexCache(optimized.data(), mesh.Indices.data(), mesh.Indices.size(), mesh.Vertices.size());
    mesh.Indices.swap(optimized);
    meshopt_optimizeOverdraw(optimized.data(), mesh.Indices.data(), mesh.Indices.size(),
                             &mesh.Vertices.front().Position.x, mesh.Vertices.size(), sizeof(Vertex), 1.05f);
    mesh.Indices.swap(optimized);
    CompactVertices(mesh);

    const auto cacheAfter = meshopt_analyzeVertexCache(mesh.Indices.data(), mesh.Indices.size(), mesh.Vertices.size(),
                                                       16, 32, 0);
    const auto fetchAfter = meshopt_analyzeVertexFetch(mesh.Indices.data(), mesh.Indices.size(), mesh.Vertices.size(),
                                                       sizeof(Vertex));
    local.OutputVertices = mesh.Vertices.size();
    local.VertexCacheAcmrAfter = cacheAfter.acmr;
    local.VertexFetchOverfetchAfter = fetchAfter.overfetch;
    if (report)
        *report = local;
    return true;
}

bool GenerateMeshLods(const MeshData &lod0, uint32_t lodCount, float triangleRatio, float targetError,
                      bool aggressive, std::vector<MeshLod> &lods, std::string *error)
{
    lods.clear();
    if (!ValidateMesh(lod0, error) || lodCount == 0 || lodCount > 8 || triangleRatio <= 0.0f ||
        triangleRatio >= 1.0f || targetError < 0.0f || targetError > 1.0f)
    {
        if (error && error->empty())
            *error = "LOD settings are outside their supported range";
        return false;
    }

    lods.push_back({lod0, 1.0f, 0.0f});
    if (lodCount == 1)
        return true;

    std::vector<float> attributes(lod0.Vertices.size() * 9u);
    for (size_t vertexIndex = 0; vertexIndex < lod0.Vertices.size(); ++vertexIndex)
    {
        const Vertex &vertex = lod0.Vertices[vertexIndex];
        const float values[9] = {vertex.Normal.x,  vertex.Normal.y,  vertex.Normal.z,
                                 vertex.Tangent.x, vertex.Tangent.y, vertex.Tangent.z,
                                 vertex.Tangent.w, vertex.UV.x,      vertex.UV.y};
        std::memcpy(attributes.data() + vertexIndex * 9u, values, sizeof(values));
    }
    const float weights[9] = {0.5f, 0.5f, 0.5f, 0.25f, 0.25f, 0.25f, 0.1f, 0.125f, 0.125f};
    size_t previousIndexCount = lod0.Indices.size();

    for (uint32_t level = 1; level < lodCount; ++level)
    {
        const float desiredRatio = std::pow(triangleRatio, static_cast<float>(level));
        size_t targetIndexCount = static_cast<size_t>(std::floor(lod0.Indices.size() * desiredRatio / 3.0f)) * 3u;
        targetIndexCount = std::clamp<size_t>(targetIndexCount, 3u, previousIndexCount >= 6u ? previousIndexCount - 3u
                                                                                            : previousIndexCount);
        std::vector<uint32_t> indices(lod0.Indices.size());
        float resultError = 0.0f;
        const float levelError = std::min(targetError * static_cast<float>(level), 1.0f);
        size_t indexCount = meshopt_simplifyWithAttributes(
            indices.data(), lod0.Indices.data(), lod0.Indices.size(), &lod0.Vertices.front().Position.x,
            lod0.Vertices.size(), sizeof(Vertex), attributes.data(), sizeof(float) * 9u, weights, 9, nullptr,
            targetIndexCount, levelError, meshopt_SimplifyLockBorder, &resultError);
        if (aggressive && indexCount > targetIndexCount)
        {
            indexCount = meshopt_simplifySloppy(indices.data(), lod0.Indices.data(), lod0.Indices.size(),
                                                &lod0.Vertices.front().Position.x, lod0.Vertices.size(), sizeof(Vertex),
                                                targetIndexCount, levelError, &resultError);
        }
        indexCount -= indexCount % 3u;
        if (indexCount < 3u || indexCount >= previousIndexCount)
            break;

        MeshData mesh;
        mesh.Vertices = lod0.Vertices;
        mesh.Indices.assign(indices.begin(), indices.begin() + static_cast<std::ptrdiff_t>(indexCount));
        if (!OptimizeMesh(mesh, false, nullptr, error))
            return false;
        lods.push_back({std::move(mesh), static_cast<float>(indexCount) / static_cast<float>(lod0.Indices.size()),
                        resultError});
        previousIndexCount = indexCount;
    }
    return true;
}

} // namespace engine::assets::cook
