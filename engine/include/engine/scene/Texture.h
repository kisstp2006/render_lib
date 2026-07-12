#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

// CPU-side image data, 8-bit per channel. Backends upload and cache GPU
// textures keyed by TextureData pointer, same pattern as MeshData/GLMesh.
struct TextureData
{
    int Width = 0;
    int Height = 0;
    int Channels = 4;
    bool SRGB = false; // true for albedo/color maps, false for normal/MRAO data maps
    std::vector<uint8_t> Pixels;
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

} // namespace textures

} // namespace engine
