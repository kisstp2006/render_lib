#pragma once

#include <string>

#include "engine/render/GpuCapabilities.h"
#include "engine/scene/Texture.h"

namespace engine
{

bool IsTextureStorageSupported(TexturePixelStorage storage,
                               const GpuCapabilityProfile& capabilities);
const char* TextureStorageName(TexturePixelStorage storage);

// Returns source when its native format is supported, otherwise builds an
// RGBA8 view in scratch from the cooker's fallback chain. Returns nullptr only
// for legacy compressed assets that contain no safety payload.
const TextureData* ResolveTextureForGpu(const TextureData& source,
                                       const GpuCapabilityProfile& capabilities,
                                       TextureData& scratch,
                                       std::string* reason = nullptr);

} // namespace engine
