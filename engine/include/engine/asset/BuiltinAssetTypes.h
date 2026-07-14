#pragma once

#include "engine/asset/SceneAsset.h"

namespace engine::assets
{

// Registers every asset type that has a working importer/transformer/runtime
// loader in this renderer. Animation/audio/font placeholders are deliberately
// not registered until their corresponding runtime subsystems exist.
bool RegisterBuiltinAssetTypes(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                               SceneComponentTypeQuery componentTypeExists = {}, std::string *error = nullptr);

} // namespace engine::assets
