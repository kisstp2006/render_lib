#include "engine/scene/MeshCombiner.h"

#include <cmath>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace engine
{
namespace
{

glm::vec3 NormalizeOr(const glm::vec3& value, const glm::vec3& fallback)
{
    const float lengthSquared = glm::dot(value, value);
    return lengthSquared > 1.0e-12f && std::isfinite(lengthSquared)
        ? value * glm::inversesqrt(lengthSquared) : fallback;
}

bool ValidateSource(const MeshData& mesh, std::string* error)
{
    if (mesh.Vertices.empty() || mesh.Indices.empty() ||
        mesh.Indices.size() % 3u != 0)
    {
        if (error)
            *error = "Mesh combining requires non-empty indexed triangle geometry";
        return false;
    }
    for (uint32_t index : mesh.Indices)
    {
        if (index >= mesh.Vertices.size())
        {
            if (error)
                *error = "Mesh combining found an out-of-range source index";
            return false;
        }
    }
    return true;
}

} // namespace

bool CombineMeshes(std::span<const MeshCombineSource> sources,
                   MeshData& output, MeshCombineReport* report,
                   std::string* error)
{
    if (error)
        error->clear();
    MeshCombineReport local;
    uint64_t totalVertices = 0;
    uint64_t totalIndices = 0;
    for (const MeshCombineSource& source : sources)
    {
        if (!source.Mesh)
            continue;
        if (!ValidateSource(*source.Mesh, error))
            return false;
        totalVertices += source.Mesh->Vertices.size();
        totalIndices += source.Mesh->Indices.size();
        if (totalVertices > std::numeric_limits<uint32_t>::max())
        {
            if (error)
                *error = "Combined mesh exceeds the 32-bit vertex-index limit";
            return false;
        }
    }
    if (totalVertices == 0 || totalIndices == 0)
    {
        if (error)
            *error = "Mesh combining received no usable source geometry";
        return false;
    }

    MeshData combined;
    combined.Vertices.reserve(static_cast<size_t>(totalVertices));
    combined.Indices.reserve(static_cast<size_t>(totalIndices));
    for (const MeshCombineSource& source : sources)
    {
        if (!source.Mesh)
            continue;
        const glm::mat3 linear(source.Transform);
        const float determinant = glm::determinant(linear);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-10f)
        {
            if (error)
                *error = "Mesh combining cannot bake a singular transform";
            return false;
        }
        const bool mirrored = determinant < 0.0f;
        const glm::mat3 normalMatrix = glm::inverseTranspose(linear);
        const uint32_t vertexOffset = static_cast<uint32_t>(combined.Vertices.size());
        for (const Vertex& sourceVertex : source.Mesh->Vertices)
        {
            Vertex vertex = sourceVertex;
            vertex.Position = glm::vec3(source.Transform * glm::vec4(sourceVertex.Position, 1.0f));
            vertex.Normal = NormalizeOr(normalMatrix * sourceVertex.Normal,
                                        glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 tangent = linear * glm::vec3(sourceVertex.Tangent);
            tangent -= vertex.Normal * glm::dot(vertex.Normal, tangent);
            tangent = NormalizeOr(tangent, glm::vec3(1.0f, 0.0f, 0.0f));
            vertex.Tangent = glm::vec4(
                tangent, sourceVertex.Tangent.w * (mirrored ? -1.0f : 1.0f));
            combined.Vertices.push_back(vertex);
        }
        for (size_t triangle = 0; triangle < source.Mesh->Indices.size(); triangle += 3)
        {
            const uint32_t a = vertexOffset + source.Mesh->Indices[triangle];
            const uint32_t b = vertexOffset + source.Mesh->Indices[triangle + 1];
            const uint32_t c = vertexOffset + source.Mesh->Indices[triangle + 2];
            combined.Indices.push_back(a);
            combined.Indices.push_back(mirrored ? c : b);
            combined.Indices.push_back(mirrored ? b : c);
        }
        ++local.SourceMeshes;
        local.MirroredMeshes += mirrored ? 1u : 0u;
        local.SourceVertices += source.Mesh->Vertices.size();
        local.SourceIndices += source.Mesh->Indices.size();
    }
    local.OutputVertices = combined.Vertices.size();
    local.OutputIndices = combined.Indices.size();
    output = std::move(combined);
    if (report)
        *report = local;
    return true;
}

} // namespace engine
