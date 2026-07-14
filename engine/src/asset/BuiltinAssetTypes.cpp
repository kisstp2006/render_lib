#include "engine/asset/BuiltinAssetTypes.h"

#include "engine/asset/ColorGradingAsset.h"
#include "engine/asset/ModelAsset.h"
#include "engine/asset/TextureAsset.h"

namespace engine::assets
{

bool RegisterBuiltinAssetTypes(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                               SceneComponentTypeQuery componentTypeExists, std::string *error)
{
    if (!RegisterTextureAssetType(registry, resourceManager, error))
        return false;
    if (!RegisterColorGradingAssetType(registry, resourceManager, error))
        return false;
    if (!RegisterCubemapAssetTypes(registry, resourceManager, error))
        return false;
    if (!RegisterModelAssetTypes(registry, resourceManager, error))
        return false;
    if (!RegisterSceneAssetType(registry, resourceManager, std::move(componentTypeExists), error))
        return false;
    return true;
}

} // namespace engine::assets
