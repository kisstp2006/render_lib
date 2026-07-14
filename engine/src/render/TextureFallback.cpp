#include "engine/render/TextureFallback.h"

namespace engine
{

bool IsTextureStorageSupported(TexturePixelStorage storage,
                               const GpuCapabilityProfile& capabilities)
{
    switch (storage)
    {
    case TexturePixelStorage::Bc1Rgb:
    case TexturePixelStorage::Bc3Rgba:
    case TexturePixelStorage::Bc5Rg:
        return capabilities.Supports(GpuFeature::TextureCompressionBc);
    case TexturePixelStorage::Bc7Rgba:
        return capabilities.Supports(GpuFeature::TextureCompressionBc7);
    case TexturePixelStorage::Astc4x4Rgba:
        return capabilities.Supports(GpuFeature::TextureCompressionAstc);
    default:
        return true;
    }
}

const char* TextureStorageName(TexturePixelStorage storage)
{
    switch (storage)
    {
    case TexturePixelStorage::Bc1Rgb: return "BC1";
    case TexturePixelStorage::Bc3Rgba: return "BC3";
    case TexturePixelStorage::Bc5Rg: return "BC5";
    case TexturePixelStorage::Bc7Rgba: return "BC7";
    case TexturePixelStorage::Astc4x4Rgba: return "ASTC_4x4";
    case TexturePixelStorage::Rgba32Float: return "RGBA32F";
    default: return "RGBA8";
    }
}

const TextureData* ResolveTextureForGpu(const TextureData& source,
                                       const GpuCapabilityProfile& capabilities,
                                       TextureData& scratch, std::string* reason)
{
    if (IsTextureStorageSupported(source.Storage, capabilities))
    {
        if (reason) reason->clear();
        return &source;
    }
    if (source.Rgba8FallbackMipLevels.empty())
    {
        if (reason)
            *reason = "the native block format is unsupported and this legacy asset has no RGBA8 fallback payload";
        return nullptr;
    }
    scratch = {};
    scratch.Width = source.Width;
    scratch.Height = source.Height;
    scratch.Channels = source.Channels;
    scratch.SRGB = source.SRGB;
    scratch.Storage = TexturePixelStorage::Rgba8;
    scratch.MipLevels = source.Rgba8FallbackMipLevels;
    scratch.Pixels = scratch.MipLevels.front().Pixels;
    scratch.Filter = source.Filter;
    scratch.AddressU = source.AddressU;
    scratch.AddressV = source.AddressV;
    scratch.AddressW = source.AddressW;
    scratch.MaxAnisotropy = source.MaxAnisotropy;
    scratch.MipBias = source.MipBias;
    if (reason)
        *reason = "unsupported native block format; selected the cooked RGBA8 fallback mip chain";
    return &scratch;
}

Material MakeVisibleFallbackMaterial()
{
    Material material;
    material.Albedo = {1.0f, 0.0f, 1.0f};
    material.Roughness = 0.65f;
    material.Metallic = 0.0f;
    material.Emissive = {0.08f, 0.0f, 0.08f};
    material.AlbedoMap = textures::MakeChecker(
        32, 4, glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.05f, 0.05f, 0.05f));
    return material;
}

} // namespace engine
