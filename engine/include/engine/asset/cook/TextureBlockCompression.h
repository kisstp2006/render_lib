#pragma once

#include "engine/scene/Texture.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::assets::cook
{

enum class BlockCompressionQuality : uint8_t
{
    Fast,
    Medium,
    High,
};

// Encodes one tightly packed RGBA8 mip into a hardware-decodable GPU block
// format. Edge texels are replicated for non-multiple-of-four dimensions.
bool CompressTextureBlocks(TexturePixelStorage storage, uint32_t width, uint32_t height,
                           std::span<const uint8_t> rgba, BlockCompressionQuality quality,
                           bool perceptual, std::vector<std::byte> &output, std::string *error = nullptr);

} // namespace engine::assets::cook
