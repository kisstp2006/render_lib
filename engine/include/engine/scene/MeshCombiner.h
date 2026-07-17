#pragma once

#include "engine/scene/Mesh.h"

#include <cstdint>
#include <span>
#include <string>

#include <glm/mat4x4.hpp>

namespace engine
{

struct MeshCombineSource
{
    const MeshData* Mesh = nullptr;
    glm::mat4 Transform{1.0f};
};

struct MeshCombineReport
{
    uint32_t SourceMeshes = 0;
    uint32_t MirroredMeshes = 0;
    uint64_t SourceVertices = 0;
    uint64_t SourceIndices = 0;
    uint64_t OutputVertices = 0;
    uint64_t OutputIndices = 0;
};

// Bakes every source transform into one indexed mesh. Positions, normals,
// tangents, tangent handedness and mirrored triangle winding are preserved.
// The operation is material-agnostic: callers must group compatible materials
// before combining geometry.
bool CombineMeshes(std::span<const MeshCombineSource> sources,
                   MeshData& output,
                   MeshCombineReport* report = nullptr,
                   std::string* error = nullptr);

} // namespace engine
