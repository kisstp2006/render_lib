#include "engine/asset/CubemapAsset.h"

#include "engine/asset/AssetFileSystem.h"
#include "engine/resource/BinaryIO.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numbers>
#include <random>

namespace engine::assets
{
namespace
{

struct Image
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Hdr = false;
    std::vector<float> Pixels;
};

std::mutex g_stbiMutex;

AssetDiagnostic Diagnostic(AssetDiagnosticSeverity severity, std::string code, std::string message,
                           const std::filesystem::path &source = {})
{
    AssetDiagnostic diagnostic;
    diagnostic.Severity = severity;
    diagnostic.Code = std::move(code);
    diagnostic.Message = std::move(message);
    diagnostic.Source = source;
    diagnostic.Step = "cubemap import";
    return diagnostic;
}

bool Decode(const std::filesystem::path &path, Image &image, std::string *error)
{
    std::scoped_lock lock(g_stbiMutex);
    int width = 0, height = 0, channels = 0;
    image.Hdr = stbi_is_hdr(path.string().c_str()) != 0;
    float *pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0)
    {
        if (error)
            *error = "Cannot decode cubemap image '" + path.generic_string() +
                     "': " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown stb_image error");
        if (pixels)
            stbi_image_free(pixels);
        return false;
    }
    if (width > 32768 || height > 32768)
    {
        stbi_image_free(pixels);
        if (error)
            *error = "Cubemap source exceeds the 32768 pixel dimension limit";
        return false;
    }
    image.Width = static_cast<uint32_t>(width);
    image.Height = static_cast<uint32_t>(height);
    image.Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return true;
}

glm::vec4 Read(const Image &image, int x, int y, bool wrapX = false)
{
    if (wrapX)
    {
        x %= static_cast<int>(image.Width);
        if (x < 0)
            x += static_cast<int>(image.Width);
    }
    else
        x = std::clamp(x, 0, static_cast<int>(image.Width) - 1);
    y = std::clamp(y, 0, static_cast<int>(image.Height) - 1);
    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
    return {image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2], image.Pixels[offset + 3]};
}

glm::vec4 Sample(const Image &image, float u, float v, bool wrapX = false)
{
    if (wrapX)
        u -= std::floor(u);
    else
        u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float x = u * image.Width - 0.5f;
    const float y = v * image.Height - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - x0;
    const float ty = y - y0;
    return glm::mix(glm::mix(Read(image, x0, y0, wrapX), Read(image, x0 + 1, y0, wrapX), tx),
                    glm::mix(Read(image, x0, y0 + 1, wrapX), Read(image, x0 + 1, y0 + 1, wrapX), tx), ty);
}

glm::vec3 CubeDirection(uint32_t face, glm::vec2 uv)
{
    switch (face)
    {
    case 0:
        return glm::normalize(glm::vec3(1.0f, -uv.y, -uv.x));
    case 1:
        return glm::normalize(glm::vec3(-1.0f, -uv.y, uv.x));
    case 2:
        return glm::normalize(glm::vec3(uv.x, 1.0f, uv.y));
    case 3:
        return glm::normalize(glm::vec3(uv.x, -1.0f, -uv.y));
    case 4:
        return glm::normalize(glm::vec3(uv.x, -uv.y, 1.0f));
    default:
        return glm::normalize(glm::vec3(-uv.x, -uv.y, -1.0f));
    }
}

std::pair<uint32_t, glm::vec2> DirectionToFace(glm::vec3 direction)
{
    const glm::vec3 absolute = glm::abs(direction);
    uint32_t face = 0;
    glm::vec2 uv{};
    if (absolute.x >= absolute.y && absolute.x >= absolute.z)
    {
        if (direction.x >= 0.0f)
        {
            face = 0;
            uv = {-direction.z, -direction.y};
        }
        else
        {
            face = 1;
            uv = {direction.z, -direction.y};
        }
        uv /= absolute.x;
    }
    else if (absolute.y >= absolute.z)
    {
        if (direction.y >= 0.0f)
        {
            face = 2;
            uv = {direction.x, direction.z};
        }
        else
        {
            face = 3;
            uv = {direction.x, -direction.z};
        }
        uv /= absolute.y;
    }
    else
    {
        if (direction.z >= 0.0f)
        {
            face = 4;
            uv = {direction.x, -direction.y};
        }
        else
        {
            face = 5;
            uv = {-direction.x, -direction.y};
        }
        uv /= absolute.z;
    }
    return {face, uv};
}

glm::vec4 ReadFace(const CubemapMip &cube, uint32_t face, int x, int y)
{
    x = std::clamp(x, 0, static_cast<int>(cube.Size) - 1);
    y = std::clamp(y, 0, static_cast<int>(cube.Size) - 1);
    const auto &pixels = cube.Faces[face];
    const size_t offset = (static_cast<size_t>(y) * cube.Size + x) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

glm::vec4 SampleCube(const CubemapMip &cube, glm::vec3 direction)
{
    const auto [face, uv] = DirectionToFace(glm::normalize(direction));
    const float x = (uv.x * 0.5f + 0.5f) * cube.Size - 0.5f;
    const float y = (uv.y * 0.5f + 0.5f) * cube.Size - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - x0;
    const float ty = y - y0;
    return glm::mix(glm::mix(ReadFace(cube, face, x0, y0), ReadFace(cube, face, x0 + 1, y0), tx),
                    glm::mix(ReadFace(cube, face, x0, y0 + 1), ReadFace(cube, face, x0 + 1, y0 + 1), tx), ty);
}

void Store(CubemapMip &cube, uint32_t face, uint32_t x, uint32_t y, glm::vec4 value)
{
    const size_t offset = (static_cast<size_t>(y) * cube.Size + x) * 4;
    std::memcpy(cube.Faces[face].data() + offset, &value[0], sizeof(float) * 4);
}

CubemapMip AllocateCube(uint32_t size)
{
    CubemapMip cube;
    cube.Size = size;
    for (auto &face : cube.Faces)
        face.resize(static_cast<size_t>(size) * size * 4);
    return cube;
}

Image CorrectFace(const Image &input, const CubemapFaceCorrection &correction)
{
    const bool swap = correction.RotationDegrees == 90 || correction.RotationDegrees == 270;
    Image result;
    result.Width = swap ? input.Height : input.Width;
    result.Height = swap ? input.Width : input.Height;
    result.Hdr = input.Hdr;
    result.Pixels.resize(static_cast<size_t>(result.Width) * result.Height * 4);
    for (uint32_t y = 0; y < result.Height; ++y)
        for (uint32_t x = 0; x < result.Width; ++x)
        {
            uint32_t sx = x, sy = y;
            if (correction.RotationDegrees == 90)
            {
                sx = y;
                sy = input.Height - 1 - x;
            }
            else if (correction.RotationDegrees == 180)
            {
                sx = input.Width - 1 - x;
                sy = input.Height - 1 - y;
            }
            else if (correction.RotationDegrees == 270)
            {
                sx = input.Width - 1 - y;
                sy = x;
            }
            if (correction.FlipHorizontal)
                sx = input.Width - 1 - sx;
            if (correction.FlipVertical)
                sy = input.Height - 1 - sy;
            const glm::vec4 value = Read(input, static_cast<int>(sx), static_cast<int>(sy));
            std::memcpy(result.Pixels.data() + (static_cast<size_t>(y) * result.Width + x) * 4, &value[0],
                        sizeof(float) * 4);
        }
    return result;
}

CubemapMip FromPanorama(const Image &panorama, uint32_t size, float rotationDegrees, float exposureEV)
{
    CubemapMip cube = AllocateCube(size);
    const float rotation = glm::radians(rotationDegrees);
    const float intensity = std::exp2(exposureEV);
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const glm::vec2 uv = (glm::vec2(x, y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                const glm::vec3 direction = CubeDirection(face, uv);
                const float longitude = std::atan2(direction.z, direction.x) + rotation;
                const float u = longitude / (2.0f * std::numbers::pi_v<float>)+0.5f;
                const float v = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / std::numbers::pi_v<float>;
                glm::vec4 color = Sample(panorama, u, v, true);
                color *= glm::vec4(intensity, intensity, intensity, 1.0f);
                Store(cube, face, x, y, glm::max(color, glm::vec4(0.0f)));
            }
    return cube;
}

CubemapMip FromFaces(const std::array<Image, 6> &images, uint32_t size,
                     const std::array<CubemapFaceCorrection, 6> &corrections, float exposureEV)
{
    CubemapMip cube = AllocateCube(size);
    const float intensity = std::exp2(exposureEV);
    for (uint32_t face = 0; face < 6; ++face)
    {
        const Image corrected = CorrectFace(images[face], corrections[face]);
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                glm::vec4 color = Sample(corrected, (x + 0.5f) / size, (y + 0.5f) / size);
                color *= glm::vec4(intensity, intensity, intensity, 1.0f);
                Store(cube, face, x, y, color);
            }
    }
    return cube;
}

CubemapMip Downsample(const CubemapMip &source)
{
    const uint32_t size = std::max(source.Size / 2, 1u);
    CubemapMip result = AllocateCube(size);
    const float offset = 0.5f / static_cast<float>(size);
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const glm::vec2 uv = (glm::vec2(x, y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                glm::vec4 sum(0.0f);
                for (glm::vec2 delta : {glm::vec2(-offset, -offset), glm::vec2(offset, -offset),
                                        glm::vec2(-offset, offset), glm::vec2(offset, offset)})
                    sum += SampleCube(source, CubeDirection(face, uv + delta));
                Store(result, face, x, y, sum * 0.25f);
            }
    return result;
}

float RadicalInverse(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

glm::vec3 TangentSample(glm::vec3 normal, glm::vec3 local)
{
    const glm::vec3 up = std::abs(normal.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
    return glm::normalize(tangent * local.x + glm::cross(normal, tangent) * local.y + normal * local.z);
}

CubemapMip Irradiance(const CubemapMip &source, uint32_t size, uint32_t sampleCount)
{
    CubemapMip result = AllocateCube(size);
    sampleCount = std::clamp(sampleCount, 8u, 256u);
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const glm::vec2 uv = (glm::vec2(x, y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                const glm::vec3 normal = CubeDirection(face, uv);
                glm::vec4 sum(0.0f);
                for (uint32_t sample = 0; sample < sampleCount; ++sample)
                {
                    const glm::vec2 xi((sample + 0.5f) / sampleCount, RadicalInverse(sample));
                    const float phi = 2.0f * std::numbers::pi_v<float> * xi.x;
                    const float radius = std::sqrt(xi.y);
                    const glm::vec3 local(std::cos(phi) * radius, std::sin(phi) * radius, std::sqrt(1.0f - xi.y));
                    sum += SampleCube(source, TangentSample(normal, local));
                }
                Store(result, face, x, y, sum / static_cast<float>(sampleCount));
            }
    return result;
}

CubemapMip Prefilter(const CubemapMip &source, uint32_t size, float roughness, uint32_t sampleCount)
{
    CubemapMip result = AllocateCube(size);
    sampleCount = std::clamp(sampleCount, 8u, 256u);
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const glm::vec2 uv = (glm::vec2(x, y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                const glm::vec3 normal = CubeDirection(face, uv);
                glm::vec4 sum(0.0f);
                float weight = 0.0f;
                const float alpha = std::max(roughness * roughness, 0.001f);
                for (uint32_t sample = 0; sample < sampleCount; ++sample)
                {
                    const glm::vec2 xi((sample + 0.5f) / sampleCount, RadicalInverse(sample));
                    const float phi = 2.0f * std::numbers::pi_v<float> * xi.x;
                    const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y));
                    const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
                    const glm::vec3 halfVector =
                        TangentSample(normal, {std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta});
                    const glm::vec3 light = glm::normalize(2.0f * glm::dot(normal, halfVector) * halfVector - normal);
                    const float nDotL = std::max(glm::dot(normal, light), 0.0f);
                    if (nDotL > 0.0f)
                    {
                        sum += SampleCube(source, light) * nDotL;
                        weight += nDotL;
                    }
                }
                Store(result, face, x, y, weight > 0.0f ? sum / weight : SampleCube(source, normal));
            }
    return result;
}

void WriteMip(resources::BinaryWriter &writer, const CubemapMip &mip)
{
    writer.WriteU32(mip.Size);
    for (const auto &face : mip.Faces)
    {
        writer.WriteU64(face.size() * sizeof(float));
        writer.WriteBytes(std::span(reinterpret_cast<const std::byte *>(face.data()), face.size() * sizeof(float)));
    }
}

bool ReadMip(resources::BinaryReader &reader, CubemapMip &mip)
{
    if (!reader.ReadU32(mip.Size) || mip.Size == 0 || mip.Size > 16384)
        return false;
    const uint64_t expected = static_cast<uint64_t>(mip.Size) * mip.Size * 4 * sizeof(float);
    for (auto &face : mip.Faces)
    {
        uint64_t size = 0;
        if (!reader.ReadU64(size) || size != expected || size > reader.Remaining())
            return false;
        std::span<const std::byte> bytes;
        if (!reader.ReadBytes(static_cast<size_t>(size), bytes))
            return false;
        face.resize(static_cast<size_t>(mip.Size) * mip.Size * 4);
        std::memcpy(face.data(), bytes.data(), bytes.size());
    }
    return true;
}

std::vector<std::byte> EncodeCubemap(const CubemapData &data)
{
    resources::BinaryWriter writer;
    writer.WriteU32(1);
    writer.WriteU32(static_cast<uint32_t>(data.MipLevels.size()));
    for (const auto &mip : data.MipLevels)
        WriteMip(writer, mip);
    writer.WriteU8(data.DiffuseIrradiance ? 1 : 0);
    if (data.DiffuseIrradiance)
        WriteMip(writer, *data.DiffuseIrradiance);
    writer.WriteU32(static_cast<uint32_t>(data.SpecularPrefilter.size()));
    for (const auto &mip : data.SpecularPrefilter)
        WriteMip(writer, mip);
    return writer.TakeData();
}

std::shared_ptr<CubemapData> LoadCubemap(const resources::ResourceLoadContext &context, std::string *error)
{
    resources::BinaryReader reader(context.Payload);
    uint32_t version = 0, mipCount = 0;
    if (!reader.ReadU32(version) || version != 1 || !reader.ReadU32(mipCount) || mipCount == 0 || mipCount > 32)
    {
        if (error)
            *error = "Malformed cubemap resource header";
        return {};
    }
    auto data = std::make_shared<CubemapData>();
    data->MipLevels.resize(mipCount);
    for (auto &mip : data->MipLevels)
        if (!ReadMip(reader, mip))
        {
            if (error)
                *error = "Malformed cubemap mip";
            return {};
        }
    uint8_t hasIrradiance = 0;
    if (!reader.ReadU8(hasIrradiance) || hasIrradiance > 1)
    {
        if (error)
            *error = "Malformed irradiance flag";
        return {};
    }
    if (hasIrradiance)
    {
        data->DiffuseIrradiance.emplace();
        if (!ReadMip(reader, *data->DiffuseIrradiance))
        {
            if (error)
                *error = "Malformed irradiance map";
            return {};
        }
    }
    uint32_t prefilterCount = 0;
    if (!reader.ReadU32(prefilterCount) || prefilterCount > 32)
    {
        if (error)
            *error = "Malformed prefilter count";
        return {};
    }
    data->SpecularPrefilter.resize(prefilterCount);
    for (auto &mip : data->SpecularPrefilter)
        if (!ReadMip(reader, mip))
        {
            if (error)
                *error = "Malformed prefiltered map";
            return {};
        }
    if (reader.Remaining() != 0)
    {
        if (error)
            *error = "Cubemap resource contains trailing data";
        return {};
    }
    return data;
}

bool ParseBool(std::string_view text, bool &value)
{
    if (text == "true" || text == "1")
    {
        value = true;
        return true;
    }
    if (text == "false" || text == "0")
    {
        value = false;
        return true;
    }
    return false;
}

template <typename T> void ParseNumber(const std::map<std::string, std::string> &values, std::string_view key, T &value)
{
    const auto found = values.find(std::string(key));
    if (found == values.end())
        return;
    if constexpr (std::is_floating_point_v<T>)
    {
        char *end = nullptr;
        const float parsed = std::strtof(found->second.c_str(), &end);
        if (end == found->second.c_str() + found->second.size() && std::isfinite(parsed))
            value = parsed;
    }
    else
    {
        try
        {
            value = static_cast<T>(std::stoul(found->second));
        }
        catch (...)
        {
        }
    }
}

std::string Bool(bool value)
{
    return value ? "true" : "false";
}
std::string Float(float value)
{
    return std::to_string(value);
}

std::vector<AssetPropertySchema> CubemapProperties()
{
    return {
        {"source_mode",
         "Source Mode",
         "Source",
         AssetPropertyType::Enumeration,
         "equirectangular",
         {"equirectangular", "six_faces"}},
        {"face_size", "Face Size", "Output", AssetPropertyType::Integer, "256", {}, 16.0, 4096.0},
        {"rotation_degrees", "Panorama Rotation", "Source", AssetPropertyType::Number, "0", {}, -360.0, 360.0},
        {"exposure_ev", "Exposure EV", "Source", AssetPropertyType::Number, "0", {}, -20.0, 20.0},
        {"generate_mips", "Generate Mipmaps", "Output", AssetPropertyType::Boolean, "true"},
        {"reduce_seams", "Reduce Seams", "Output", AssetPropertyType::Boolean, "true"},
        {"generate_irradiance", "Bake Irradiance", "IBL", AssetPropertyType::Boolean, "true"},
        {"irradiance_size",
         "Irradiance Size",
         "IBL",
         AssetPropertyType::Integer,
         "32",
         {},
         4.0,
         128.0,
         {},
         "generate_irradiance",
         "true"},
        {"generate_prefilter", "Bake GGX Prefilter", "IBL", AssetPropertyType::Boolean, "true"},
        {"prefilter_size",
         "Prefilter Size",
         "IBL",
         AssetPropertyType::Integer,
         "128",
         {},
         16.0,
         1024.0,
         {},
         "generate_prefilter",
         "true"},
        {"filter_samples", "Filter Samples", "IBL", AssetPropertyType::Integer, "64", {}, 8.0, 256.0},
    };
}

std::vector<AssetPropertySchema> SkyboxProperties()
{
    return {
        {"cubemap",
         "Cubemap",
         "Source",
         AssetPropertyType::AssetReference,
         "",
         {},
         {},
         {},
         std::string(kCubemapAssetType)},
        {"exposure_ev", "Exposure EV", "Appearance", AssetPropertyType::Number, "0", {}, -20.0, 20.0},
        {"intensity", "Intensity", "Appearance", AssetPropertyType::Number, "1", {}, 0.0, 100.0},
        {"tint", "Tint", "Appearance", AssetPropertyType::Color, "1,1,1"},
        {"saturation", "Saturation", "Appearance", AssetPropertyType::Number, "1", {}, 0.0, 4.0},
        {"rotation_degrees", "Rotation", "Appearance", AssetPropertyType::Number, "0", {}, -360.0, 360.0},
        {"blur_mip", "Blur Mip", "Appearance", AssetPropertyType::Number, "0", {}, 0.0, 16.0},
        {"environment_lighting", "Environment Lighting", "Lighting", AssetPropertyType::Boolean, "true"},
        {"diffuse_irradiance",
         "Diffuse Irradiance",
         "Lighting",
         AssetPropertyType::Boolean,
         "true",
         {},
         {},
         {},
         {},
         "environment_lighting",
         "true"},
        {"specular_ibl",
         "Specular IBL",
         "Lighting",
         AssetPropertyType::Boolean,
         "true",
         {},
         {},
         {},
         {},
         "environment_lighting",
         "true"},
        {"fallback_color", "Fallback Color", "Fallback", AssetPropertyType::Color, "0.04,0.04,0.04"},
    };
}

glm::vec3 ParseVec3(std::string_view text, glm::vec3 fallback)
{
    std::string copy(text);
    char *cursor = copy.data();
    for (int index = 0; index < 3; ++index)
    {
        char *end = nullptr;
        const float value = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(value))
            return fallback;
        fallback[index] = value;
        if (index < 2)
        {
            if (*end != ',')
                return fallback;
            cursor = end + 1;
        }
    }
    return fallback;
}

std::string Vec3(glm::vec3 value)
{
    return Float(value.x) + ',' + Float(value.y) + ',' + Float(value.z);
}

std::vector<std::byte> EncodeSkybox(const SkyboxAssetSettings &settings)
{
    resources::BinaryWriter writer;
    writer.WriteU32(1);
    writer.WriteU64(settings.Cubemap.Guid.High);
    writer.WriteU64(settings.Cubemap.Guid.Low);
    writer.WriteF32(settings.ExposureEV);
    writer.WriteF32(settings.Intensity);
    writer.WriteF32(settings.Tint.x);
    writer.WriteF32(settings.Tint.y);
    writer.WriteF32(settings.Tint.z);
    writer.WriteF32(settings.Saturation);
    writer.WriteF32(settings.RotationDegrees);
    writer.WriteF32(settings.BlurMip);
    writer.WriteU8(settings.EnvironmentLighting ? 1 : 0);
    writer.WriteU8(settings.DiffuseIrradiance ? 1 : 0);
    writer.WriteU8(settings.SpecularIbl ? 1 : 0);
    writer.WriteU8(0);
    writer.WriteString(settings.SkyMaterial);
    writer.WriteF32(settings.FallbackColor.x);
    writer.WriteF32(settings.FallbackColor.y);
    writer.WriteF32(settings.FallbackColor.z);
    return writer.TakeData();
}

std::shared_ptr<SkyboxData> LoadSkybox(const resources::ResourceLoadContext &context, std::string *error)
{
    resources::BinaryReader reader(context.Payload);
    uint32_t version = 0;
    uint64_t high = 0, low = 0;
    uint8_t lighting = 0, diffuse = 0, specular = 0, reserved = 0;
    auto result = std::make_shared<SkyboxData>();
    if (!reader.ReadU32(version) || version != 1 || !reader.ReadU64(high) || !reader.ReadU64(low) ||
        !reader.ReadF32(result->ExposureEV) || !reader.ReadF32(result->Intensity) || !reader.ReadF32(result->Tint.x) ||
        !reader.ReadF32(result->Tint.y) || !reader.ReadF32(result->Tint.z) || !reader.ReadF32(result->Saturation) ||
        !reader.ReadF32(result->RotationDegrees) || !reader.ReadF32(result->BlurMip) || !reader.ReadU8(lighting) ||
        !reader.ReadU8(diffuse) || !reader.ReadU8(specular) || !reader.ReadU8(reserved) ||
        !reader.ReadString(result->SkyMaterial, 4096) || !reader.ReadF32(result->FallbackColor.x) ||
        !reader.ReadF32(result->FallbackColor.y) || !reader.ReadF32(result->FallbackColor.z) || reader.Remaining() != 0)
    {
        if (error)
            *error = reader.Error().empty() ? "Malformed skybox resource" : reader.Error();
        return {};
    }
    result->Cubemap.Guid = {high, low};
    result->EnvironmentLighting = lighting != 0;
    result->DiffuseIrradiance = diffuse != 0;
    result->SpecularIbl = specular != 0;
    return result;
}

} // namespace

bool ValidateCubemapFaces(std::span<const CubemapFaceInfo> faces, std::vector<AssetDiagnostic> &diagnostics)
{
    if (faces.size() != 6)
    {
        diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "cubemap_face_count",
                                         "A six-face cubemap requires exactly six images."));
        return false;
    }
    const CubemapFaceInfo reference = faces.front();
    for (size_t index = 0; index < faces.size(); ++index)
    {
        if (faces[index].Width == 0 || faces[index].Width != faces[index].Height)
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "cubemap_face_not_square",
                                             "Cubemap face " + std::to_string(index) + " is not square."));
        if (faces[index].Width != reference.Width || faces[index].Height != reference.Height)
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "cubemap_face_size_mismatch",
                                             "All cubemap faces must have equal dimensions."));
        if (faces[index].Hdr != reference.Hdr)
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::Warning, "cubemap_hdr_mismatch",
                                             "Cubemap faces mix HDR and LDR sources."));
    }
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const AssetDiagnostic &diagnostic) {
        return diagnostic.Severity == AssetDiagnosticSeverity::FatalError;
    });
}

CubemapAssetSettings ReadCubemapAssetSettings(const AssetDescriptor &descriptor, std::string_view profile)
{
    CubemapAssetSettings settings;
    const auto values = descriptor.ResolveSettings(profile);
    auto get = [&](std::string_view key, std::string_view fallback) {
        const auto found = values.find(std::string(key));
        return found == values.end() ? std::string(fallback) : found->second;
    };
    settings.SourceMode = get("source_mode", "equirectangular") == "six_faces" ? CubemapSourceMode::SixFaces
                                                                               : CubemapSourceMode::Equirectangular;
    ParseNumber(values, "face_size", settings.FaceSize);
    ParseBool(get("generate_mips", "true"), settings.GenerateMipmaps);
    ParseBool(get("reduce_seams", "true"), settings.ReduceSeams);
    ParseBool(get("generate_irradiance", "true"), settings.GenerateIrradiance);
    ParseNumber(values, "irradiance_size", settings.IrradianceSize);
    ParseBool(get("generate_prefilter", "true"), settings.GenerateSpecularPrefilter);
    ParseNumber(values, "prefilter_size", settings.PrefilterSize);
    ParseNumber(values, "filter_samples", settings.FilterSampleCount);
    ParseNumber(values, "rotation_degrees", settings.RotationDegrees);
    ParseNumber(values, "exposure_ev", settings.ExposureEV);
    for (size_t face = 0; face < 6; ++face)
    {
        const std::string prefix = "face_" + std::to_string(face) + '_';
        ParseNumber(values, prefix + "rotation", settings.FaceCorrections[face].RotationDegrees);
        ParseBool(get(prefix + "flip_h", "false"), settings.FaceCorrections[face].FlipHorizontal);
        ParseBool(get(prefix + "flip_v", "false"), settings.FaceCorrections[face].FlipVertical);
    }
    return settings;
}

void WriteCubemapAssetSettings(AssetDescriptor &descriptor, const CubemapAssetSettings &settings)
{
    auto &values = descriptor.Settings;
    values["source_mode"] = settings.SourceMode == CubemapSourceMode::SixFaces ? "six_faces" : "equirectangular";
    values["face_size"] = std::to_string(settings.FaceSize);
    values["generate_mips"] = Bool(settings.GenerateMipmaps);
    values["reduce_seams"] = Bool(settings.ReduceSeams);
    values["generate_irradiance"] = Bool(settings.GenerateIrradiance);
    values["irradiance_size"] = std::to_string(settings.IrradianceSize);
    values["generate_prefilter"] = Bool(settings.GenerateSpecularPrefilter);
    values["prefilter_size"] = std::to_string(settings.PrefilterSize);
    values["filter_samples"] = std::to_string(settings.FilterSampleCount);
    values["rotation_degrees"] = Float(settings.RotationDegrees);
    values["exposure_ev"] = Float(settings.ExposureEV);
    for (size_t face = 0; face < 6; ++face)
    {
        const std::string prefix = "face_" + std::to_string(face) + '_';
        values[prefix + "rotation"] = std::to_string(settings.FaceCorrections[face].RotationDegrees);
        values[prefix + "flip_h"] = Bool(settings.FaceCorrections[face].FlipHorizontal);
        values[prefix + "flip_v"] = Bool(settings.FaceCorrections[face].FlipVertical);
    }
}

SkyboxAssetSettings ReadSkyboxAssetSettings(const AssetDescriptor &descriptor, std::string_view profile)
{
    SkyboxAssetSettings settings;
    const auto values = descriptor.ResolveSettings(profile);
    auto get = [&](std::string_view key, std::string_view fallback) {
        const auto found = values.find(std::string(key));
        return found == values.end() ? std::string(fallback) : found->second;
    };
    if (const auto guid = AssetGuid::Parse(get("cubemap", "")))
        settings.Cubemap.Guid = *guid;
    ParseNumber(values, "exposure_ev", settings.ExposureEV);
    ParseNumber(values, "intensity", settings.Intensity);
    settings.Tint = ParseVec3(get("tint", "1,1,1"), settings.Tint);
    ParseNumber(values, "saturation", settings.Saturation);
    ParseNumber(values, "rotation_degrees", settings.RotationDegrees);
    ParseNumber(values, "blur_mip", settings.BlurMip);
    ParseBool(get("environment_lighting", "true"), settings.EnvironmentLighting);
    ParseBool(get("diffuse_irradiance", "true"), settings.DiffuseIrradiance);
    ParseBool(get("specular_ibl", "true"), settings.SpecularIbl);
    settings.SkyMaterial = get("sky_material", "");
    settings.FallbackColor = ParseVec3(get("fallback_color", "0.04,0.04,0.04"), settings.FallbackColor);
    return settings;
}

void WriteSkyboxAssetSettings(AssetDescriptor &descriptor, const SkyboxAssetSettings &settings)
{
    auto &values = descriptor.Settings;
    values["cubemap"] = settings.Cubemap.Guid.ToString();
    values["exposure_ev"] = Float(settings.ExposureEV);
    values["intensity"] = Float(settings.Intensity);
    values["tint"] = Vec3(settings.Tint);
    values["saturation"] = Float(settings.Saturation);
    values["rotation_degrees"] = Float(settings.RotationDegrees);
    values["blur_mip"] = Float(settings.BlurMip);
    values["environment_lighting"] = Bool(settings.EnvironmentLighting);
    values["diffuse_irradiance"] = Bool(settings.DiffuseIrradiance);
    values["specular_ibl"] = Bool(settings.SpecularIbl);
    values["sky_material"] = settings.SkyMaterial;
    values["fallback_color"] = Vec3(settings.FallbackColor);
}

ImportResult CreateCubemapAsset(AssetPipeline &pipeline, const std::filesystem::path &panorama,
                                const std::filesystem::path &destinationDirectory)
{
    return pipeline.CreateAssetFromSource(panorama, destinationDirectory, kCubemapAssetType, "panorama");
}

ImportResult CreateCubemapAsset(AssetPipeline &pipeline, const std::array<std::filesystem::path, 6> &faces,
                                const std::filesystem::path &destinationDirectory, std::string_view name,
                                std::string *error)
{
    ImportResult result;
    AssetDescriptor descriptor;
    descriptor.Guid = AssetGuid::Generate();
    descriptor.Type = std::string(kCubemapAssetType);
    descriptor.Name = std::string(name);
    descriptor.State = AssetImportState::Dirty;
    for (const auto &face : faces)
    {
        std::string pathError;
        auto relative = NormalizeProjectRelative(pipeline.Config().ProjectRoot, face, &pathError);
        if (relative.empty())
        {
            if (error)
                *error = pathError;
            return result;
        }
        descriptor.Sources.push_back(relative);
        descriptor.SourceDependencies.push_back(relative);
    }
    CubemapAssetSettings settings;
    settings.SourceMode = CubemapSourceMode::SixFaces;
    WriteCubemapAssetSettings(descriptor, settings);
    const auto path =
        pipeline.GetAssetDescriptorPath(faces[0], *pipeline.Types().Find(kCubemapAssetType), destinationDirectory)
            .parent_path() /
        (std::string(name) + "." + std::string(kCubemapDescriptorExtension));
    if (!SaveAssetDescriptor(path, descriptor, error) || !pipeline.Database().AddOrUpdate(path, descriptor, error))
        return result;
    result.GeneratedAssets.push_back({descriptor, path, false});
    result.Succeeded = true;
    return result;
}

bool CreateSkyboxAsset(AssetPipeline &pipeline, AssetGuid cubemap, std::string_view name,
                       const std::filesystem::path &descriptorPath, AssetGuid *createdGuid, std::string *error)
{
    const auto dependency = pipeline.Database().Find(cubemap);
    if (!dependency || dependency->Descriptor.Type != kCubemapAssetType)
    {
        if (error)
            *error = "Skybox requires a valid cubemap asset";
        return false;
    }
    AssetDescriptor descriptor;
    descriptor.Guid = AssetGuid::Generate();
    descriptor.Type = std::string(kSkyboxAssetType);
    descriptor.Name = std::string(name);
    descriptor.AssetDependencies = {cubemap};
    descriptor.State = AssetImportState::Dirty;
    SkyboxAssetSettings settings;
    settings.Cubemap.Guid = cubemap;
    WriteSkyboxAssetSettings(descriptor, settings);
    if (!SaveAssetDescriptor(descriptorPath, descriptor, error) ||
        !pipeline.Database().AddOrUpdate(descriptorPath, descriptor, error))
        return false;
    if (createdGuid)
        *createdGuid = descriptor.Guid;
    return true;
}

bool RegisterCubemapAssetTypes(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                               std::string *error)
{
    AssetTypeRegistration cube;
    cube.TypeId = std::string(kCubemapAssetType);
    cube.DisplayName = "Cubemap";
    cube.DescriptorExtension = std::string(kCubemapDescriptorExtension);
    cube.SourceExtensions = {"hdr", "png", "jpg", "jpeg", "tga", "bmp"};
    cube.ImportModes = {{"panorama", "Equirectangular panorama", 100}};
    cube.RuntimeType = std::string(kCubemapResourceType);
    cube.Icon = "cubemap";
    cube.Properties = CubemapProperties();
    cube.Import = [](const AssetImportRequest &request) {
        ImportResult result;
        std::string pathError;
        auto relative = NormalizeProjectRelative(request.ProjectRoot, request.SourcePath, &pathError);
        if (relative.empty())
        {
            result.Diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "source_outside_project",
                                                    pathError, request.SourcePath));
            return result;
        }
        ImportedAsset asset;
        asset.DescriptorPath = request.DestinationDirectory / (request.SourcePath.stem().generic_string() + "." +
                                                               std::string(kCubemapDescriptorExtension));
        AssetDescriptor existing;
        std::string loadError;
        if (LoadAssetDescriptor(asset.DescriptorPath, existing, &loadError) && existing.Type == kCubemapAssetType)
        {
            asset.Descriptor = std::move(existing);
            asset.Reused = true;
            asset.Descriptor.Sources = {relative};
            asset.Descriptor.SourceDependencies = {relative};
            asset.Descriptor.State = AssetImportState::Dirty;
            result.GeneratedAssets.push_back(std::move(asset));
            result.Succeeded = true;
            return result;
        }
        asset.Descriptor.Guid = AssetGuid::Generate();
        asset.Descriptor.Type = std::string(kCubemapAssetType);
        asset.Descriptor.Name = request.SourcePath.stem().generic_string();
        asset.Descriptor.Sources = {relative};
        asset.Descriptor.SourceDependencies = {relative};
        asset.Descriptor.State = AssetImportState::Dirty;
        WriteCubemapAssetSettings(asset.Descriptor, {});
        result.GeneratedAssets.push_back(std::move(asset));
        result.Succeeded = true;
        return result;
    };
    cube.Validate = [](const AssetDescriptor &descriptor, const AssetValidationContext &context) {
        std::vector<AssetDiagnostic> diagnostics;
        const auto settings = ReadCubemapAssetSettings(descriptor, context.Profile);
        const size_t expected = settings.SourceMode == CubemapSourceMode::SixFaces ? 6 : 1;
        if (descriptor.Sources.size() != expected)
            diagnostics.push_back(
                Diagnostic(AssetDiagnosticSeverity::FatalError, "cubemap_source_count",
                           "Cubemap source mode requires " + std::to_string(expected) + " source image(s)."));
        if (settings.FaceSize < 16 || settings.FaceSize > 4096 || (settings.FaceSize & (settings.FaceSize - 1)) != 0)
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "cubemap_face_size",
                                             "Cubemap face size must be a power of two between 16 and 4096."));
        return diagnostics;
    };
    cube.Transform = [](const AssetDescriptor &descriptor, const AssetTransformContext &context,
                        AssetTransformOutput &output, std::string *transformError) {
        AssetDescriptor resolved = descriptor;
        resolved.Settings = context.ResolvedSettings;
        resolved.PlatformOverrides.clear();
        const auto settings = ReadCubemapAssetSettings(resolved);
        CubemapData data;
        if (settings.SourceMode == CubemapSourceMode::Equirectangular)
        {
            Image panorama;
            if (!Decode(context.ProjectRoot / descriptor.Sources[0], panorama, transformError))
                return false;
            data.MipLevels.push_back(
                FromPanorama(panorama, settings.FaceSize, settings.RotationDegrees, settings.ExposureEV));
        }
        else
        {
            std::array<Image, 6> images;
            std::array<CubemapFaceInfo, 6> infos;
            for (size_t face = 0; face < 6; ++face)
            {
                if (!Decode(context.ProjectRoot / descriptor.Sources[face], images[face], transformError))
                    return false;
                infos[face] = {images[face].Width, images[face].Height, images[face].Hdr};
            }
            if (!ValidateCubemapFaces(infos, output.Diagnostics))
            {
                if (transformError)
                    *transformError = "Six-face cubemap validation failed";
                return false;
            }
            data.MipLevels.push_back(
                FromFaces(images, settings.FaceSize, settings.FaceCorrections, settings.ExposureEV));
        }
        if (settings.GenerateMipmaps)
            while (data.MipLevels.back().Size > 1)
                data.MipLevels.push_back(Downsample(data.MipLevels.back()));
        if (settings.GenerateIrradiance)
            data.DiffuseIrradiance = Irradiance(data.MipLevels[0], settings.IrradianceSize, settings.FilterSampleCount);
        if (settings.GenerateSpecularPrefilter)
        {
            uint32_t size = settings.PrefilterSize;
            const uint32_t count = static_cast<uint32_t>(std::floor(std::log2(size))) + 1;
            for (uint32_t mip = 0; mip < count; ++mip)
            {
                const float roughness = count > 1 ? static_cast<float>(mip) / (count - 1) : 0.0f;
                data.SpecularPrefilter.push_back(
                    Prefilter(data.MipLevels[0], size, roughness, settings.FilterSampleCount));
                size = std::max(size / 2, 1u);
            }
        }
        output.RuntimeType = std::string(kCubemapResourceType);
        output.ResourceVersion = 1;
        output.Compression = "rle";
        output.Payload = EncodeCubemap(data);
        output.Statistics["face_size"] = settings.FaceSize;
        output.Statistics["mip_levels"] = data.MipLevels.size();
        output.Statistics["payload_bytes"] = output.Payload.size();
        return true;
    };
    cube.Preview = [](const AssetDescriptor &descriptor, const AssetPreviewRequest &request, AssetPreview &preview,
                      std::string *previewError) {
        if (descriptor.Sources.empty())
        {
            if (previewError)
                *previewError = "Cubemap has no source";
            return false;
        }
        Image image;
        if (!Decode(request.ProjectRoot / descriptor.Sources.front(), image, previewError))
            return false;
        const float scale = std::min({1.0f, static_cast<float>(request.MaximumWidth) / image.Width,
                                      static_cast<float>(request.MaximumHeight) / image.Height});
        const uint32_t width = std::max(static_cast<uint32_t>(image.Width * scale), 1u),
                       height = std::max(static_cast<uint32_t>(image.Height * scale), 1u);
        preview.Kind = "environment";
        preview.MimeType = "application/x-rgba32f";
        preview.Width = width;
        preview.Height = height;
        preview.Bytes.resize(static_cast<size_t>(width) * height * 16);
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
            {
                const glm::vec4 color = Sample(image, (x + 0.5f) / width, (y + 0.5f) / height, true);
                std::memcpy(preview.Bytes.data() + (static_cast<size_t>(y) * width + x) * 16, &color[0], 16);
            }
        preview.CacheKey = HashFile(request.ProjectRoot / descriptor.Sources.front(), nullptr);
        return true;
    };
    cube.Inspector = [](const AssetDescriptor &) { return CubemapProperties(); };
    cube.ProfileDefaults = [](std::string_view profile) {
        std::map<std::string, std::string> values;
        if (profile == "mobile")
        {
            values["face_size"] = "512";
            values["prefilter_size"] = "128";
            values["filter_samples"] = "32";
        }
        return values;
    };
    if (!registry.Register(std::move(cube), error))
        return false;
    if (!resourceManager.RegisterLoader<CubemapData>(std::string(kCubemapResourceType), LoadCubemap, error))
    {
        registry.Unregister(kCubemapAssetType);
        return false;
    }

    AssetTypeRegistration sky;
    sky.TypeId = std::string(kSkyboxAssetType);
    sky.DisplayName = "Skybox";
    sky.DescriptorExtension = std::string(kSkyboxDescriptorExtension);
    sky.RuntimeType = std::string(kSkyboxResourceType);
    sky.Icon = "skybox";
    sky.Properties = SkyboxProperties();
    sky.Validate = [](const AssetDescriptor &descriptor, const AssetValidationContext &) {
        std::vector<AssetDiagnostic> diagnostics;
        const auto settings = ReadSkyboxAssetSettings(descriptor);
        if (!settings.Cubemap)
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "skybox_cubemap_missing",
                                             "Skybox requires a cubemap asset reference."));
        return diagnostics;
    };
    sky.Transform = [](const AssetDescriptor &descriptor, const AssetTransformContext &context,
                       AssetTransformOutput &output, std::string *) {
        AssetDescriptor resolved = descriptor;
        resolved.Settings = context.ResolvedSettings;
        resolved.PlatformOverrides.clear();
        const auto settings = ReadSkyboxAssetSettings(resolved);
        output.RuntimeType = std::string(kSkyboxResourceType);
        output.ResourceVersion = 1;
        output.Payload = EncodeSkybox(settings);
        output.AssetDependencies = {settings.Cubemap.Guid};
        return true;
    };
    sky.Inspector = [](const AssetDescriptor &) { return SkyboxProperties(); };
    if (!registry.Register(std::move(sky), error))
        return false;
    if (!resourceManager.RegisterLoader<SkyboxData>(std::string(kSkyboxResourceType), LoadSkybox, error))
    {
        registry.Unregister(kSkyboxAssetType);
        return false;
    }
    return true;
}

} // namespace engine::assets
