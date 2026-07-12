#include "engine/scene/Texture.h"
#include "engine/core/Log.h"

#include <stb_image.h>

namespace engine::textures {

namespace {

std::shared_ptr<TextureData> CopyDecoded(unsigned char* pixels, int width, int height, bool srgb)
{
    auto data = std::make_shared<TextureData>();
    data->Width = width;
    data->Height = height;
    data->Channels = 4;
    data->SRGB = srgb;
    data->Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return data;
}

} // namespace

std::shared_ptr<TextureData> LoadFromFile(const std::string& path, bool srgb)
{
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels)
    {
        log::Error("Failed to load texture: " + path + " (" + stbi_failure_reason() + ")");
        return nullptr;
    }

    return CopyDecoded(pixels, width, height, srgb);
}

std::shared_ptr<TextureData> LoadFromMemory(const uint8_t* bytes, size_t size, bool srgb, const std::string& debugName)
{
    if (!bytes || size == 0)
    {
        log::Error("Empty texture data: " + debugName);
        return nullptr;
    }

    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels)
    {
        log::Error("Failed to decode texture: " + debugName + " (" + stbi_failure_reason() + ")");
        return nullptr;
    }

    return CopyDecoded(pixels, width, height, srgb);
}

std::shared_ptr<TextureData> MakeSolidColor(glm::vec4 color, bool srgb)
{
    auto data = std::make_shared<TextureData>();
    data->Width = 1;
    data->Height = 1;
    data->SRGB = srgb;
    data->Pixels = {
        static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f),
    };
    return data;
}

std::shared_ptr<TextureData> MakeChecker(int size, int cells, glm::vec3 colorA, glm::vec3 colorB)
{
    auto data = std::make_shared<TextureData>();
    data->Width = size;
    data->Height = size;
    data->SRGB = true;
    data->Pixels.resize(static_cast<size_t>(size) * size * 4);

    const int cellSize = size / cells;
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const bool a = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            const glm::vec3 c = a ? colorA : colorB;
            uint8_t* px = &data->Pixels[(static_cast<size_t>(y) * size + x) * 4];
            px[0] = static_cast<uint8_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
            px[1] = static_cast<uint8_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
            px[2] = static_cast<uint8_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
            px[3] = 255;
        }
    }
    return data;
}

std::shared_ptr<TextureData> MakeFlatNormal()
{
    auto data = std::make_shared<TextureData>();
    data->Width = 1;
    data->Height = 1;
    data->SRGB = false;
    data->Pixels = {128, 128, 255, 255};
    return data;
}

} // namespace engine::textures
