#pragma once

#include "engine/asset/CubemapAsset.h"

#include <functional>
#include <glm/gtc/quaternion.hpp>

namespace engine::assets
{

inline constexpr std::string_view kSceneAssetType = "scene";
inline constexpr std::string_view kSceneDescriptorExtension = "sla-scene";
inline constexpr std::string_view kSceneResourceType = "scene";

struct SceneComponentRecord
{
    std::string Type;
    uint32_t Version = 1;
    bool Enabled = true;
    std::map<std::string, std::string> Properties;
};

struct SceneObjectRecord
{
    AssetGuid Guid;
    AssetGuid Parent;
    std::string Name;
    std::string Tag;
    uint32_t Layer = 0;
    bool Active = true;
    glm::vec3 Position{0.0f};
    glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 Scale{1.0f};
    std::vector<SceneComponentRecord> Components;
    std::vector<AssetGuid> AssetReferences;
    AssetGuid Prefab;
};

struct SceneAssetData
{
    uint32_t FormatVersion = 1;
    std::vector<SceneObjectRecord> Objects;
    AssetHandle<SkyboxData> Skybox;
    AssetGuid ActiveCameraObject;
    std::vector<std::string> Layers;
    std::map<std::string, std::string> Environment;
    std::map<std::string, std::string> Lighting;
    // Never emitted into the cooked resource.
    std::map<std::string, std::string> EditorMetadata;
};

using SceneComponentTypeQuery = std::function<bool(std::string_view type, uint32_t version)>;

void WriteSceneAssetData(AssetDescriptor &descriptor, const SceneAssetData &scene);
bool ReadSceneAssetData(const AssetDescriptor &descriptor, SceneAssetData &scene, std::string *error = nullptr);
bool ValidateSceneReferences(const SceneAssetData &scene, const AssetDatabase &database,
                             const SceneComponentTypeQuery &componentTypeExists,
                             std::vector<AssetDiagnostic> &diagnostics);
bool CreateSceneAsset(AssetPipeline &pipeline, const SceneAssetData &scene, std::string_view name,
                      const std::filesystem::path &descriptorPath, AssetGuid *createdGuid = nullptr,
                      std::string *error = nullptr);
bool RegisterSceneAssetType(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                            SceneComponentTypeQuery componentTypeExists = {}, std::string *error = nullptr);

} // namespace engine::assets
