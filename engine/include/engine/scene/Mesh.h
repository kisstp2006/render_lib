#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

struct Vertex
{
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec4 Tangent; // xyz = tangent, w = bitangent sign
    glm::vec2 UV;
};

// CPU-side mesh data. Render backends upload this into their own GPU buffers
// and cache the result (see GLMesh) rather than owning geometry themselves.
struct MeshData
{
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
    // Mutable runtime-generated geometry increments Revision after replacing
    // either stream. Native backends use it to refresh cached GPU buffers
    // without changing the shared CPU-side mesh identity.
    uint64_t Revision = 1;
    bool RuntimeMutable = false;
};

// One optional, lower-detail representation of a MeshData.  The level-zero
// mesh continues to live in MeshInstance::Mesh, so existing scenes remain
// source-compatible.  RelativeError is the maximum geometric deviation
// relative to the source mesh's bounding-sphere diameter; it is the value
// produced by meshoptimizer and lets the renderer make a screen-space choice.
struct MeshLodLevel
{
    std::shared_ptr<MeshData> Mesh;
    float TriangleRatio = 1.0f;
    float RelativeError = 0.0f;
};

namespace primitives {

MeshData MakeSphere(float radius = 1.0f, int stacks = 32, int slices = 32);
MeshData MakeCube(float halfExtent = 1.0f);
MeshData MakePlane(float size = 1.0f, int subdivisions = 1);

} // namespace primitives

} // namespace engine
