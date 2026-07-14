#pragma once

#include "engine/asset/TextureAsset.h"
#include "engine/asset/cook/MeshProcessing.h"
#include "engine/scene/Material.h"
#include "engine/scene/Mesh.h"

#include <glm/mat4x4.hpp>

namespace engine::assets
{

inline constexpr std::string_view kMaterialAssetType = "material";
inline constexpr std::string_view kMaterialDescriptorExtension = "sla-material";
inline constexpr std::string_view kMaterialResourceType = "material";
inline constexpr std::string_view kStaticMeshAssetType = "static-mesh";
inline constexpr std::string_view kStaticMeshDescriptorExtension = "sla-mesh";
inline constexpr std::string_view kStaticMeshResourceType = "static-mesh";

enum class MeshAxis : uint8_t
{
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ
};
enum class MeshHandedness : uint8_t
{
    RightHanded,
    LeftHanded
};

struct MaterialAssetData
{
    std::string Shader = "pbr";
    std::vector<std::string> Features;
    Material Value;
    AssetHandle<TextureData> BaseColor;
    AssetHandle<TextureData> Normal;
    AssetHandle<TextureData> MetallicRoughness;
    AssetHandle<TextureData> Occlusion;
    AssetHandle<TextureData> Emissive;
    bool DoubleSided = false;
    bool DepthTest = true;
    bool DepthWrite = true;
};

struct StaticMeshPart
{
    std::shared_ptr<MeshData> Mesh;
    // Simplified levels after LOD0; Mesh remains the backward-compatible LOD0.
    std::vector<cook::MeshLod> Lods;
    glm::mat4 Transform{1.0f};
    AssetHandle<MaterialAssetData> Material;
    std::string MaterialSlot;
};

struct StaticMeshData
{
    std::vector<StaticMeshPart> Parts;
    cook::CollisionData Collision;
    glm::vec3 BoundsMinimum{0.0f};
    glm::vec3 BoundsMaximum{0.0f};
};

struct StaticMeshAssetSettings
{
    MeshAxis SourceUp = MeshAxis::PositiveY;
    MeshAxis SourceForward = MeshAxis::PositiveZ;
    MeshHandedness Handedness = MeshHandedness::RightHanded;
    glm::vec3 Translation{0.0f};
    glm::vec3 RotationDegrees{0.0f};
    glm::vec3 Scale{1.0f};
    bool BakeTransform = false;
    bool FlattenHierarchy = false;
    bool RemoveEmptyNodes = true;
    bool ImportHiddenNodes = false;
    bool MergeMeshes = false;
    bool ImportNormals = true;
    bool GenerateNormals = true;
    bool ImportTangents = true;
    bool GenerateTangents = true;
    bool WeldVertices = false;
    bool RemoveDegenerateTriangles = true;
    bool OptimizeIndices = true;
    bool GenerateLods = false;
    uint32_t LodCount = 1;
    float LodTriangleRatio = 0.5f;
    float LodTargetError = 0.02f;
    bool LodAggressive = false;
    bool ImportCollision = false;
    cook::CollisionType Collision = cook::CollisionType::None;
    float CollisionTriangleRatio = 0.25f;
    float CollisionSimplificationError = 0.02f;
    uint32_t CollisionMaxConvexHulls = 8;
    uint32_t CollisionMaxVerticesPerHull = 64;
    uint32_t CollisionVoxelResolution = 100000;
    float CollisionConvexErrorPercent = 1.0f;
};

glm::mat4 ConvertCoordinateSystem(MeshAxis sourceUp, MeshAxis sourceForward, MeshHandedness handedness);
glm::mat4 BuildMeshImportTransform(const StaticMeshAssetSettings &settings);
bool ValidateMeshImportSettings(const StaticMeshAssetSettings &settings, std::vector<AssetDiagnostic> &diagnostics);
StaticMeshAssetSettings ReadStaticMeshAssetSettings(const AssetDescriptor &descriptor, std::string_view profile = {});
void WriteStaticMeshAssetSettings(AssetDescriptor &descriptor, const StaticMeshAssetSettings &settings);

bool RegisterModelAssetTypes(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                             std::string *error = nullptr);

} // namespace engine::assets
