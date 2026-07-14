#pragma once

#include "engine/asset/AssetTypeRegistry.h"
#include "engine/asset/ColorGrading.h"
#include "engine/resource/ResourceManager.h"

namespace engine::assets
{

inline constexpr std::string_view kColorGradingLutAssetType = "color-grading-lut";
inline constexpr std::string_view kColorGradingLutDescriptorExtension = "sla-lut";
inline constexpr std::string_view kColorGradingLutResourceType = "color-grading-lut";

bool RegisterColorGradingAssetType(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                                   std::string *error = nullptr);

} // namespace engine::assets
