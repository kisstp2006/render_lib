#pragma once

#include "engine/asset/AssetTypeRegistry.h"
#include "engine/resource/ResourceManager.h"
#include "engine/scene/Texture.h"

#include <array>
#include <cstdint>
#include <string>

namespace engine::assets
{

inline constexpr std::string_view kTexture2DAssetType = "texture2d";
inline constexpr std::string_view kTexture2DResourceType = "texture2d";
inline constexpr std::string_view kTexture2DDescriptorExtension = "sla-texture";

enum class TextureUsage : uint8_t
{
    Auto,
    Color,
    LinearData,
    Hdr,
    NormalMap,
    InvertedNormalMap,
    HeightMap,
    Mask,
    UiSprite,
};

enum class TextureColorSpace : uint8_t
{
    Auto,
    Srgb,
    Linear
};
enum class TextureRotation : uint8_t
{
    None,
    Degrees90,
    Degrees180,
    Degrees270
};
enum class TextureMipFilter : uint8_t
{
    Box,
    Kaiser
};
enum class TextureCompressionQuality : uint8_t
{
    None,
    Fast,
    Medium,
    High
};

struct TextureAssetSettings
{
    TextureUsage Usage = TextureUsage::Auto;
    TextureColorSpace ColorSpace = TextureColorSpace::Auto;
    bool FlipVertical = false;
    bool FlipHorizontal = false;
    TextureRotation Rotation = TextureRotation::None;
    bool InvertGreen = false;
    bool PremultiplyAlpha = false;
    bool RemoveAlpha = false;
    bool ForceAlpha = false;
    bool BumpToNormal = false;
    float BumpStrength = 1.0f;
    std::array<std::string, 4> Swizzle{"r", "g", "b", "a"};
    std::array<std::string, 4> ChannelSources{"source0.r", "source0.g", "source0.b", "source0.a"};

    bool GenerateMips = true;
    TextureMipFilter MipFilter = TextureMipFilter::Kaiser;
    bool PreserveAlphaCoverage = false;
    float AlphaThreshold = 0.5f;
    int MaximumMipLevel = -1;
    uint32_t MinimumResolution = 1;
    uint32_t MaximumResolution = 16384;
    uint32_t Downscale = 1;

    TextureCompressionQuality Compression = TextureCompressionQuality::High;
    std::string PreferredPixelFormat = "auto";
    TextureFilterMode Filter = TextureFilterMode::Anisotropic;
    TextureAddressMode AddressU = TextureAddressMode::Repeat;
    TextureAddressMode AddressV = TextureAddressMode::Repeat;
    TextureAddressMode AddressW = TextureAddressMode::Repeat;
    float MaximumAnisotropy = 8.0f;
    float MipBias = 0.0f;

    bool DilateTransparentColor = true;
};

enum class RuntimeTextureFormat : uint8_t
{
    Rgba8Unorm,
    Rgba8Srgb,
    Rgba32Float,
    Bc1Unorm,
    Bc1Srgb,
    Bc3Unorm,
    Bc3Srgb,
    Bc5Unorm,
    Bc7Unorm,
    Bc7Srgb,
    Astc4x4Unorm,
    Astc4x4Srgb,
};

TextureUsage DetectTextureUsage(const std::filesystem::path &path, bool hdr);
RuntimeTextureFormat ChooseTextureFormat(const TextureAssetSettings &settings, bool hasAlpha, bool hdr,
                                         std::string_view platform,
                                         std::vector<AssetDiagnostic> *diagnostics = nullptr);
uint32_t CalculateTextureMipCount(uint32_t width, uint32_t height, const TextureAssetSettings &settings);
bool ValidateTextureSettings(const TextureAssetSettings &settings, std::vector<AssetDiagnostic> &diagnostics);

TextureAssetSettings ReadTextureAssetSettings(const AssetDescriptor &descriptor, std::string_view profile = {});
void WriteTextureAssetSettings(AssetDescriptor &descriptor, const TextureAssetSettings &settings);

bool RegisterTextureAssetType(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                              std::string *error = nullptr);

} // namespace engine::assets
