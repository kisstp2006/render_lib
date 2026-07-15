#pragma once

#include "engine/asset/SceneAsset.h"
#include "engine/runtime/World.h"

#include <filesystem>
#include <string>

namespace engine::assets
{

// Converts the complete behavior World (hierarchy, stable entity GUIDs,
// transforms, activation and reflected component properties) to the existing
// scene asset representation used by the asset pipeline.
bool SerializeWorld(runtime::World& world, SceneAssetData& scene,
                    runtime::EntityId activeCamera = runtime::kInvalidEntity,
                    std::string* error = nullptr);

// Component types must be registered before loading. Unknown components or
// malformed properties fail with an entity/component/property-qualified error.
bool DeserializeWorld(const SceneAssetData& scene, runtime::World& world,
                      runtime::EntityId* activeCamera = nullptr,
                      std::string* error = nullptr);

// Standalone editor save/load path using the versioned .sla-scene asset
// descriptor envelope. Existing descriptor identity and non-scene metadata
// are preserved when saving over a file.
bool SaveWorldScene(const std::filesystem::path& path, runtime::World& world,
                    runtime::EntityId activeCamera = runtime::kInvalidEntity,
                    std::string* error = nullptr);
bool LoadWorldScene(const std::filesystem::path& path, runtime::World& world,
                    runtime::EntityId* activeCamera = nullptr,
                    std::string* error = nullptr);

} // namespace engine::assets
