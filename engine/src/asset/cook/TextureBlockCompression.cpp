#include "engine/asset/cook/TextureBlockCompression.h"

#include <astcenc.h>
#include <bc7enc.h>
#include <rgbcx.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace engine::assets::cook
{
namespace
{

void InitializeBcEncoders()
{
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
        bc7enc_compress_block_init();
    });
}

uint32_t RgbcxLevel(BlockCompressionQuality quality)
{
    switch (quality)
    {
    case BlockCompressionQuality::Fast:
        return 2;
    case BlockCompressionQuality::Medium:
        return 10;
    case BlockCompressionQuality::High:
        return 18;
    }
    return 10;
}

float AstcQuality(BlockCompressionQuality quality)
{
    switch (quality)
    {
    case BlockCompressionQuality::Fast:
        return ASTCENC_PRE_FAST;
    case BlockCompressionQuality::Medium:
        return ASTCENC_PRE_MEDIUM;
    case BlockCompressionQuality::High:
        return ASTCENC_PRE_THOROUGH;
    }
    return ASTCENC_PRE_MEDIUM;
}

std::array<uint8_t, 64> GatherBlock(std::span<const uint8_t> rgba, uint32_t width, uint32_t height,
                                    uint32_t blockX, uint32_t blockY)
{
    std::array<uint8_t, 64> block{};
    for (uint32_t y = 0; y < 4; ++y)
    {
        const uint32_t sourceY = std::min(blockY * 4u + y, height - 1u);
        for (uint32_t x = 0; x < 4; ++x)
        {
            const uint32_t sourceX = std::min(blockX * 4u + x, width - 1u);
            const size_t source = (static_cast<size_t>(sourceY) * width + sourceX) * 4u;
            const size_t destination = (static_cast<size_t>(y) * 4u + x) * 4u;
            std::memcpy(block.data() + destination, rgba.data() + source, 4u);
        }
    }
    return block;
}

bool CompressAstc(uint32_t width, uint32_t height, std::span<const uint8_t> rgba,
                  BlockCompressionQuality quality, bool perceptual, std::vector<std::byte> &output,
                  std::string *error)
{
    astcenc_config config{};
    const astcenc_profile profile = perceptual ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR;
    const unsigned int flags = perceptual ? ASTCENC_FLG_USE_PERCEPTUAL : 0u;
    astcenc_error result = astcenc_config_init(profile, 4, 4, 1, AstcQuality(quality), flags, &config);
    if (result != ASTCENC_SUCCESS)
    {
        if (error)
            *error = std::string("ASTC configuration failed: ") + astcenc_get_error_string(result);
        return false;
    }

    astcenc_context *context = nullptr;
    result = astcenc_context_alloc(&config, 1, &context);
    if (result != ASTCENC_SUCCESS)
    {
        if (error)
            *error = std::string("ASTC context allocation failed: ") + astcenc_get_error_string(result);
        return false;
    }

    void *slice = const_cast<uint8_t *>(rgba.data());
    astcenc_image image{width, height, 1, ASTCENC_TYPE_U8, &slice};
    const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
    output.resize(TextureMipByteSize(TexturePixelStorage::Astc4x4Rgba, width, height));
    result = astcenc_compress_image(context, &image, &swizzle, reinterpret_cast<uint8_t *>(output.data()),
                                    output.size(), 0);
    astcenc_context_free(context);
    if (result != ASTCENC_SUCCESS)
    {
        output.clear();
        if (error)
            *error = std::string("ASTC compression failed: ") + astcenc_get_error_string(result);
        return false;
    }
    return true;
}

} // namespace

bool CompressTextureBlocks(TexturePixelStorage storage, uint32_t width, uint32_t height,
                           std::span<const uint8_t> rgba, BlockCompressionQuality quality,
                           bool perceptual, std::vector<std::byte> &output, std::string *error)
{
    output.clear();
    if (!IsBlockCompressed(storage))
    {
        if (error)
            *error = "Requested texture storage is not a block-compressed format";
        return false;
    }
    if (width == 0 || height == 0 || rgba.size() != static_cast<size_t>(width) * height * 4u)
    {
        if (error)
            *error = "Block compressor requires a non-empty, tightly packed RGBA8 image";
        return false;
    }
    if (storage == TexturePixelStorage::Astc4x4Rgba)
        return CompressAstc(width, height, rgba, quality, perceptual, output, error);

    InitializeBcEncoders();
    const uint32_t blocksX = (width + 3u) / 4u;
    const uint32_t blocksY = (height + 3u) / 4u;
    const size_t bytesPerBlock = storage == TexturePixelStorage::Bc1Rgb ? 8u : 16u;
    output.resize(static_cast<size_t>(blocksX) * blocksY * bytesPerBlock);

    bc7enc_compress_block_params bc7Parameters{};
    bc7enc_compress_block_params_init(&bc7Parameters);
    if (!perceptual)
        bc7enc_compress_block_params_init_linear_weights(&bc7Parameters);
    switch (quality)
    {
    case BlockCompressionQuality::Fast:
        bc7Parameters.m_max_partitions = 8;
        bc7Parameters.m_uber_level = 0;
        break;
    case BlockCompressionQuality::Medium:
        bc7Parameters.m_max_partitions = 32;
        bc7Parameters.m_uber_level = 1;
        break;
    case BlockCompressionQuality::High:
        bc7Parameters.m_max_partitions = BC7ENC_MAX_PARTITIONS;
        bc7Parameters.m_uber_level = 2;
        break;
    }

    for (uint32_t blockY = 0; blockY < blocksY; ++blockY)
    {
        for (uint32_t blockX = 0; blockX < blocksX; ++blockX)
        {
            const auto block = GatherBlock(rgba, width, height, blockX, blockY);
            std::byte *destination = output.data() +
                                     (static_cast<size_t>(blockY) * blocksX + blockX) * bytesPerBlock;
            switch (storage)
            {
            case TexturePixelStorage::Bc1Rgb:
                rgbcx::encode_bc1(RgbcxLevel(quality), destination, block.data(), true, false);
                break;
            case TexturePixelStorage::Bc3Rgba:
                rgbcx::encode_bc3(RgbcxLevel(quality), destination, block.data());
                break;
            case TexturePixelStorage::Bc5Rg:
                rgbcx::encode_bc5(destination, block.data(), 0, 1, 4);
                break;
            case TexturePixelStorage::Bc7Rgba:
                bc7enc_compress_block(destination, block.data(), &bc7Parameters);
                break;
            default:
                if (error)
                    *error = "Unsupported BC texture storage";
                output.clear();
                return false;
            }
        }
    }
    return true;
}

} // namespace engine::assets::cook
