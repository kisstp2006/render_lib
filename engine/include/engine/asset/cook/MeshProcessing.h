#pragma once

#include "engine/scene/Mesh.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::assets::cook
{

struct MeshOptimizationReport
{
    uint64_t InputVertices = 0;
    uint64_t OutputVertices = 0;
    uint64_t Triangles = 0;
    float VertexCacheAcmrBefore = 0.0f;
    float VertexCacheAcmrAfter = 0.0f;
    float VertexFetchOverfetchBefore = 0.0f;
    float VertexFetchOverfetchAfter = 0.0f;
};

struct MeshLod
{
    MeshData Mesh;
    float TriangleRatio = 1.0f;
    float RelativeError = 0.0f;
};

bool OptimizeMesh(MeshData &mesh, bool weldVertices, MeshOptimizationReport *report = nullptr,
                  std::string *error = nullptr);
bool GenerateMeshLods(const MeshData &lod0, uint32_t lodCount, float triangleRatio, float targetError,
                      bool aggressive, std::vector<MeshLod> &lods, std::string *error = nullptr);

enum class CollisionType : uint8_t
{
    None,
    TriangleMesh,
    SimplifiedTriangleMesh,
    ConvexDecomposition,
};

struct CollisionCookSettings
{
    CollisionType Type = CollisionType::None;
    float TriangleRatio = 0.25f;
    float SimplificationError = 0.02f;
    uint32_t MaxConvexHulls = 8;
    uint32_t MaxVerticesPerHull = 64;
    uint32_t VoxelResolution = 100000;
    float ConvexErrorPercent = 1.0f;
};

struct CollisionSourceMesh
{
    const MeshData *Mesh = nullptr;
    glm::mat4 Transform{1.0f};
};

struct CollisionBvhNode
{
    glm::vec3 BoundsMinimum{0.0f};
    glm::vec3 BoundsMaximum{0.0f};
    uint32_t LeftChild = UINT32_MAX;
    uint32_t RightChild = UINT32_MAX;
    uint32_t FirstTriangle = 0;
    uint32_t TriangleCount = 0;
};

struct CollisionMesh
{
    bool Convex = false;
    std::vector<glm::vec3> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<CollisionBvhNode> Bvh;
};

struct CollisionData
{
    CollisionType Type = CollisionType::None;
    std::vector<CollisionMesh> Meshes;
};

bool ValidateCollisionCookSettings(const CollisionCookSettings &settings, std::string *error = nullptr);
bool CookCollision(std::span<const CollisionSourceMesh> sources, const CollisionCookSettings &settings,
                   CollisionData &output, std::string *error = nullptr);

} // namespace engine::assets::cook
