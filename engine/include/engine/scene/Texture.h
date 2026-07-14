#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

enum class TexturePixelStorage : uint8_t
{
    Rgba8,
    Rgba32Float,
    Bc1Rgb,
    Bc3Rgba,
    Bc5Rg,
    Bc7Rgba,
    Astc4x4Rgba,
};

constexpr bool IsBlockCompressed(TexturePixelStorage storage)
{
    return storage >= TexturePixelStorage::Bc1Rgb;
}

constexpr size_t TextureMipByteSize(TexturePixelStorage storage, uint32_t width, uint32_t height)
{
    if (storage == TexturePixelStorage::Rgba32Float)
        return static_cast<size_t>(width) * height * sizeof(float) * 4u;
    if (storage == TexturePixelStorage::Rgba8)
        return static_cast<size_t>(width) * height * 4u;
    const size_t blocksX = (static_cast<size_t>(width) + 3u) / 4u;
    const size_t blocksY = (static_cast<size_t>(height) + 3u) / 4u;
    const size_t bytesPerBlock = storage == TexturePixelStorage::Bc1Rgb ? 8u : 16u;
    return blocksX * blocksY * bytesPerBlock;
}

enum class TextureFilterMode : uint8_t
{
    Nearest,
    Bilinear,
    Trilinear,
    Anisotropic,
};

enum class TextureAddressMode : uint8_t
{
    Repeat,
    Clamp,
    Mirror,
    Border,
};

struct TextureMipData
{
    int Width = 0;
    int Height = 0;
    std::vector<uint8_t> Pixels;
    std::vector<float> FloatPixels;
};

// Renderer-neutral CPU texture data. Cooked assets may provide a complete mip
// chain and sampler state; legacy source loaders can still provide only level 0.
struct TextureData
{
    int Width = 0;
    int Height = 0;
    int Channels = 4;
    bool SRGB = false; // true for albedo/color maps, false for normal/MRAO data maps
    TexturePixelStorage Storage = TexturePixelStorage::Rgba8;
    std::vector<uint8_t> Pixels;
    std::vector<float> FloatPixels;
    std::vector<TextureMipData> MipLevels;
    TextureFilterMode Filter = TextureFilterMode::Anisotropic;
    TextureAddressMode AddressU = TextureAddressMode::Repeat;
    TextureAddressMode AddressV = TextureAddressMode::Repeat;
    TextureAddressMode AddressW = TextureAddressMode::Repeat;
    float MaxAnisotropy = 8.0f;
    float MipBias = 0.0f;
};

namespace textures {

// Loads via stb_image (PNG/JPG/TGA/BMP/HDR-as-LDR...). Returns nullptr and
// logs on failure.
std::shared_ptr<TextureData> LoadFromFile(const std::string& path, bool srgb, bool flipVertically = true);
std::shared_ptr<TextureData> LoadFromMemory(const uint8_t* bytes, size_t size, bool srgb,
                                            const std::string& debugName = "memory image", bool flipVertically = true);

std::shared_ptr<TextureData> MakeSolidColor(glm::vec4 color, bool srgb = true);
std::shared_ptr<TextureData> MakeChecker(int size, int cells, glm::vec3 colorA, glm::vec3 colorB);
// Flat +Z tangent-space normal (128, 128, 255) - the "no-op" normal map.
std::shared_ptr<TextureData> MakeFlatNormal();
std::shared_ptr<TextureData> MakeLightCookie(int size = 256);

} // namespace textures

} // namespace engine
