#include "engine/asset/TextureAsset.h"

#include "engine/asset/AssetFileSystem.h"
#include "engine/asset/cook/TextureBlockCompression.h"
#include "engine/resource/BinaryIO.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>

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

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::string Bool(bool value)
{
    return value ? "true" : "false";
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

template <typename Integer> bool ParseInteger(std::string_view text, Integer &value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool ParseFloat(std::string_view text, float &value)
{
    std::string copy(text);
    char *end = nullptr;
    value = std::strtof(copy.c_str(), &end);
    return end == copy.c_str() + copy.size() && std::isfinite(value);
}

std::string Float(float value)
{
    std::ostringstream stream;
    stream.precision(std::numeric_limits<float>::max_digits10);
    stream << value;
    return stream.str();
}

std::string ToString(TextureUsage usage)
{
    switch (usage)
    {
    case TextureUsage::Auto:
        return "auto";
    case TextureUsage::Color:
        return "color";
    case TextureUsage::LinearData:
        return "linear";
    case TextureUsage::Hdr:
        return "hdr";
    case TextureUsage::NormalMap:
        return "normal";
    case TextureUsage::InvertedNormalMap:
        return "normal_inverted";
    case TextureUsage::HeightMap:
        return "height";
    case TextureUsage::Mask:
        return "mask";
    case TextureUsage::UiSprite:
        return "ui";
    }
    return "auto";
}

bool ParseEnum(std::string_view text, TextureUsage &usage)
{
    if (text == "auto")
        usage = TextureUsage::Auto;
    else if (text == "color")
        usage = TextureUsage::Color;
    else if (text == "linear")
        usage = TextureUsage::LinearData;
    else if (text == "hdr")
        usage = TextureUsage::Hdr;
    else if (text == "normal")
        usage = TextureUsage::NormalMap;
    else if (text == "normal_inverted")
        usage = TextureUsage::InvertedNormalMap;
    else if (text == "height")
        usage = TextureUsage::HeightMap;
    else if (text == "mask")
        usage = TextureUsage::Mask;
    else if (text == "ui")
        usage = TextureUsage::UiSprite;
    else
        return false;
    return true;
}

std::string ToString(TextureColorSpace value)
{
    switch (value)
    {
    case TextureColorSpace::Auto:
        return "auto";
    case TextureColorSpace::Srgb:
        return "srgb";
    case TextureColorSpace::Linear:
        return "linear";
    }
    return "auto";
}

bool ParseEnum(std::string_view text, TextureColorSpace &value)
{
    if (text == "auto")
        value = TextureColorSpace::Auto;
    else if (text == "srgb")
        value = TextureColorSpace::Srgb;
    else if (text == "linear")
        value = TextureColorSpace::Linear;
    else
        return false;
    return true;
}

std::string ToString(TextureRotation value)
{
    switch (value)
    {
    case TextureRotation::None:
        return "0";
    case TextureRotation::Degrees90:
        return "90";
    case TextureRotation::Degrees180:
        return "180";
    case TextureRotation::Degrees270:
        return "270";
    }
    return "0";
}

bool ParseEnum(std::string_view text, TextureRotation &value)
{
    if (text == "0")
        value = TextureRotation::None;
    else if (text == "90")
        value = TextureRotation::Degrees90;
    else if (text == "180")
        value = TextureRotation::Degrees180;
    else if (text == "270")
        value = TextureRotation::Degrees270;
    else
        return false;
    return true;
}

std::string ToString(TextureMipFilter value)
{
    return value == TextureMipFilter::Kaiser ? "kaiser" : "box";
}

bool ParseEnum(std::string_view text, TextureMipFilter &value)
{
    if (text == "box")
        value = TextureMipFilter::Box;
    else if (text == "kaiser")
        value = TextureMipFilter::Kaiser;
    else
        return false;
    return true;
}

std::string ToString(TextureCompressionQuality value)
{
    switch (value)
    {
    case TextureCompressionQuality::None:
        return "none";
    case TextureCompressionQuality::Fast:
        return "fast";
    case TextureCompressionQuality::Medium:
        return "medium";
    case TextureCompressionQuality::High:
        return "high";
    }
    return "high";
}

bool ParseEnum(std::string_view text, TextureCompressionQuality &value)
{
    if (text == "none")
        value = TextureCompressionQuality::None;
    else if (text == "fast")
        value = TextureCompressionQuality::Fast;
    else if (text == "medium")
        value = TextureCompressionQuality::Medium;
    else if (text == "high")
        value = TextureCompressionQuality::High;
    else
        return false;
    return true;
}

std::string ToString(TextureFilterMode value)
{
    switch (value)
    {
    case TextureFilterMode::Nearest:
        return "nearest";
    case TextureFilterMode::Bilinear:
        return "bilinear";
    case TextureFilterMode::Trilinear:
        return "trilinear";
    case TextureFilterMode::Anisotropic:
        return "anisotropic";
    }
    return "anisotropic";
}

bool ParseEnum(std::string_view text, TextureFilterMode &value)
{
    if (text == "nearest")
        value = TextureFilterMode::Nearest;
    else if (text == "bilinear")
        value = TextureFilterMode::Bilinear;
    else if (text == "trilinear")
        value = TextureFilterMode::Trilinear;
    else if (text == "anisotropic")
        value = TextureFilterMode::Anisotropic;
    else
        return false;
    return true;
}

std::string ToString(TextureAddressMode value)
{
    switch (value)
    {
    case TextureAddressMode::Repeat:
        return "repeat";
    case TextureAddressMode::Clamp:
        return "clamp";
    case TextureAddressMode::Mirror:
        return "mirror";
    case TextureAddressMode::Border:
        return "border";
    }
    return "repeat";
}

bool ParseEnum(std::string_view text, TextureAddressMode &value)
{
    if (text == "repeat")
        value = TextureAddressMode::Repeat;
    else if (text == "clamp")
        value = TextureAddressMode::Clamp;
    else if (text == "mirror")
        value = TextureAddressMode::Mirror;
    else if (text == "border")
        value = TextureAddressMode::Border;
    else
        return false;
    return true;
}

AssetDiagnostic MakeDiagnostic(AssetDiagnosticSeverity severity, std::string code, std::string message,
                               const std::filesystem::path &source = {}, std::string suggestion = {})
{
    AssetDiagnostic diagnostic;
    diagnostic.Severity = severity;
    diagnostic.Code = std::move(code);
    diagnostic.Message = std::move(message);
    diagnostic.Source = source;
    diagnostic.Step = "texture import";
    diagnostic.Suggestion = std::move(suggestion);
    return diagnostic;
}

bool DecodeImage(const std::filesystem::path &path, Image &image, std::string *error)
{
    std::scoped_lock lock(g_stbiMutex);
    const std::string native = path.string();
    int width = 0, height = 0, channels = 0;
    image.Hdr = stbi_is_hdr(native.c_str()) != 0;
    if (image.Hdr)
    {
        float *pixels = stbi_loadf(native.c_str(), &width, &height, &channels, 4);
        if (!pixels)
        {
            if (error)
                *error = "Failed to decode HDR image '" + path.generic_string() +
                         "': " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
            return false;
        }
        image.Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
        stbi_image_free(pixels);
    }
    else
    {
        uint8_t *pixels = stbi_load(native.c_str(), &width, &height, &channels, 4);
        if (!pixels)
        {
            if (error)
                *error = "Failed to decode image '" + path.generic_string() +
                         "': " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
            return false;
        }
        image.Pixels.resize(static_cast<size_t>(width) * height * 4);
        std::transform(pixels, pixels + image.Pixels.size(), image.Pixels.begin(),
                       [](uint8_t value) { return static_cast<float>(value) / 255.0f; });
        stbi_image_free(pixels);
    }
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536)
    {
        if (error)
            *error = "Image dimensions are invalid or excessive: " + path.generic_string();
        return false;
    }
    image.Width = static_cast<uint32_t>(width);
    image.Height = static_cast<uint32_t>(height);
    for (float &value : image.Pixels)
    {
        if (!std::isfinite(value))
        {
            if (error)
                *error = "Image contains non-finite pixels: " + path.generic_string();
            return false;
        }
        if (!image.Hdr)
            value = std::clamp(value, 0.0f, 1.0f);
    }
    return true;
}

std::array<float, 4> Pixel(const Image &image, uint32_t x, uint32_t y)
{
    x = std::min(x, image.Width - 1);
    y = std::min(y, image.Height - 1);
    const float *pixel = &image.Pixels[(static_cast<size_t>(y) * image.Width + x) * 4];
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

std::array<float, 4> SampleBilinear(const Image &image, float u, float v)
{
    const float x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.Width - 1);
    const float y = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.Height - 1);
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(x0 + 1, image.Width - 1);
    const uint32_t y1 = std::min(y0 + 1, image.Height - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const auto a = Pixel(image, x0, y0);
    const auto b = Pixel(image, x1, y0);
    const auto c = Pixel(image, x0, y1);
    const auto d = Pixel(image, x1, y1);
    std::array<float, 4> result{};
    for (size_t channel = 0; channel < 4; ++channel)
        result[channel] = std::lerp(std::lerp(a[channel], b[channel], fx), std::lerp(c[channel], d[channel], fx), fy);
    return result;
}

Image Resize(const Image &source, uint32_t width, uint32_t height)
{
    if (source.Width == width && source.Height == height)
        return source;
    Image output;
    output.Width = width;
    output.Height = height;
    output.Hdr = source.Hdr;
    output.Pixels.resize(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
        {
            const auto sample = SampleBilinear(source, (static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                                               (static_cast<float>(y) + 0.5f) / static_cast<float>(height));
            std::copy(sample.begin(), sample.end(),
                      output.Pixels.begin() + static_cast<ptrdiff_t>((static_cast<size_t>(y) * width + x) * 4));
        }
    return output;
}

void Flip(Image &image, bool horizontal, bool vertical)
{
    if (!horizontal && !vertical)
        return;
    std::vector<float> output(image.Pixels.size());
    for (uint32_t y = 0; y < image.Height; ++y)
        for (uint32_t x = 0; x < image.Width; ++x)
        {
            const uint32_t sourceX = horizontal ? image.Width - 1 - x : x;
            const uint32_t sourceY = vertical ? image.Height - 1 - y : y;
            const size_t destination = (static_cast<size_t>(y) * image.Width + x) * 4;
            const size_t source = (static_cast<size_t>(sourceY) * image.Width + sourceX) * 4;
            std::copy_n(image.Pixels.begin() + static_cast<ptrdiff_t>(source), 4,
                        output.begin() + static_cast<ptrdiff_t>(destination));
        }
    image.Pixels = std::move(output);
}

void Rotate(Image &image, TextureRotation rotation)
{
    if (rotation == TextureRotation::None)
        return;
    const bool swapDimensions = rotation == TextureRotation::Degrees90 || rotation == TextureRotation::Degrees270;
    Image output;
    output.Width = swapDimensions ? image.Height : image.Width;
    output.Height = swapDimensions ? image.Width : image.Height;
    output.Hdr = image.Hdr;
    output.Pixels.resize(image.Pixels.size());
    for (uint32_t y = 0; y < output.Height; ++y)
        for (uint32_t x = 0; x < output.Width; ++x)
        {
            uint32_t sourceX = x, sourceY = y;
            if (rotation == TextureRotation::Degrees90)
            {
                sourceX = y;
                sourceY = image.Height - 1 - x;
            }
            else if (rotation == TextureRotation::Degrees180)
            {
                sourceX = image.Width - 1 - x;
                sourceY = image.Height - 1 - y;
            }
            else if (rotation == TextureRotation::Degrees270)
            {
                sourceX = image.Width - 1 - y;
                sourceY = x;
            }
            const size_t source = (static_cast<size_t>(sourceY) * image.Width + sourceX) * 4;
            const size_t destination = (static_cast<size_t>(y) * output.Width + x) * 4;
            std::copy_n(image.Pixels.begin() + static_cast<ptrdiff_t>(source), 4,
                        output.Pixels.begin() + static_cast<ptrdiff_t>(destination));
        }
    image = std::move(output);
}

int ChannelIndex(std::string_view channel)
{
    if (channel == "r" || channel == "x")
        return 0;
    if (channel == "g" || channel == "y")
        return 1;
    if (channel == "b" || channel == "z")
        return 2;
    if (channel == "a" || channel == "w")
        return 3;
    return -1;
}

float ResolveChannel(std::string_view expression, const std::vector<Image> &sources, float u, float v, bool &valid)
{
    if (expression == "white" || expression == "one" || expression == "1")
        return 1.0f;
    if (expression == "black" || expression == "zero" || expression == "0")
        return 0.0f;
    if (!expression.starts_with("source"))
    {
        valid = false;
        return 0.0f;
    }
    const size_t dot = expression.find('.');
    if (dot == std::string_view::npos)
    {
        valid = false;
        return 0.0f;
    }
    uint32_t sourceIndex = 0;
    if (!ParseInteger(expression.substr(6, dot - 6), sourceIndex) || sourceIndex >= sources.size())
    {
        valid = false;
        return 0.0f;
    }
    const int channel = ChannelIndex(expression.substr(dot + 1));
    if (channel < 0)
    {
        valid = false;
        return 0.0f;
    }
    return SampleBilinear(sources[sourceIndex], u, v)[static_cast<size_t>(channel)];
}

bool PackChannels(const std::vector<Image> &sources, const TextureAssetSettings &settings, Image &output,
                  std::string *error)
{
    if (sources.empty())
        return false;
    output.Width = sources[0].Width;
    output.Height = sources[0].Height;
    output.Hdr = std::any_of(sources.begin(), sources.end(), [](const Image &image) { return image.Hdr; });
    output.Pixels.resize(static_cast<size_t>(output.Width) * output.Height * 4);
    bool valid = true;
    for (uint32_t y = 0; y < output.Height; ++y)
        for (uint32_t x = 0; x < output.Width; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(output.Width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(output.Height);
            for (size_t channel = 0; channel < 4; ++channel)
                output.Pixels[(static_cast<size_t>(y) * output.Width + x) * 4 + channel] =
                    ResolveChannel(settings.ChannelSources[channel], sources, u, v, valid);
        }
    if (!valid)
    {
        if (error)
            *error = "Invalid channel source expression. Use sourceN.r/g/b/a, black or white.";
        return false;
    }
    return true;
}

void ApplySwizzle(Image &image, const std::array<std::string, 4> &swizzle, bool &valid)
{
    for (size_t pixelIndex = 0; pixelIndex < image.Pixels.size(); pixelIndex += 4)
    {
        const std::array<float, 4> source = {image.Pixels[pixelIndex], image.Pixels[pixelIndex + 1],
                                             image.Pixels[pixelIndex + 2], image.Pixels[pixelIndex + 3]};
        for (size_t outputChannel = 0; outputChannel < 4; ++outputChannel)
        {
            const std::string &expression = swizzle[outputChannel];
            const int channel = ChannelIndex(expression);
            if (channel >= 0)
                image.Pixels[pixelIndex + outputChannel] = source[static_cast<size_t>(channel)];
            else if (expression == "white" || expression == "one" || expression == "1")
                image.Pixels[pixelIndex + outputChannel] = 1.0f;
            else if (expression == "black" || expression == "zero" || expression == "0")
                image.Pixels[pixelIndex + outputChannel] = 0.0f;
            else
                valid = false;
        }
    }
}

void ConvertBumpToNormal(Image &image, float strength)
{
    std::vector<float> output(image.Pixels.size());
    auto height = [&](int x, int y) {
        x = std::clamp(x, 0, static_cast<int>(image.Width) - 1);
        y = std::clamp(y, 0, static_cast<int>(image.Height) - 1);
        return image.Pixels[(static_cast<size_t>(y) * image.Width + static_cast<uint32_t>(x)) * 4];
    };
    for (uint32_t y = 0; y < image.Height; ++y)
        for (uint32_t x = 0; x < image.Width; ++x)
        {
            const float dx = (height(static_cast<int>(x) + 1, y) - height(static_cast<int>(x) - 1, y)) * strength;
            const float dy = (height(x, static_cast<int>(y) + 1) - height(x, static_cast<int>(y) - 1)) * strength;
            const float inverseLength = 1.0f / std::sqrt(dx * dx + dy * dy + 1.0f);
            const size_t index = (static_cast<size_t>(y) * image.Width + x) * 4;
            output[index] = -dx * inverseLength * 0.5f + 0.5f;
            output[index + 1] = -dy * inverseLength * 0.5f + 0.5f;
            output[index + 2] = inverseLength * 0.5f + 0.5f;
            output[index + 3] = image.Pixels[index + 3];
        }
    image.Pixels = std::move(output);
}

void DilateTransparent(Image &image)
{
    std::vector<float> current = image.Pixels;
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        bool changed = false;
        std::vector<float> next = current;
        for (uint32_t y = 0; y < image.Height; ++y)
            for (uint32_t x = 0; x < image.Width; ++x)
            {
                const size_t index = (static_cast<size_t>(y) * image.Width + x) * 4;
                if (current[index + 3] > 0.001f)
                    continue;
                float color[3]{};
                uint32_t samples = 0;
                constexpr int offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto &offset : offsets)
                {
                    const int nx = static_cast<int>(x) + offset[0];
                    const int ny = static_cast<int>(y) + offset[1];
                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(image.Width) || ny >= static_cast<int>(image.Height))
                        continue;
                    const size_t neighbour = (static_cast<size_t>(ny) * image.Width + static_cast<uint32_t>(nx)) * 4;
                    if (current[neighbour + 3] <= 0.001f)
                        continue;
                    for (size_t channel = 0; channel < 3; ++channel)
                        color[channel] += current[neighbour + channel];
                    ++samples;
                }
                if (samples != 0)
                {
                    for (size_t channel = 0; channel < 3; ++channel)
                        next[index + channel] = color[channel] / static_cast<float>(samples);
                    next[index + 3] = 0.0001f; // carries color into the next dilation ring
                    changed = true;
                }
            }
        current = std::move(next);
        if (!changed)
            break;
    }
    for (size_t index = 0; index < current.size(); index += 4)
        if (current[index + 3] <= 0.001f)
            current[index + 3] = 0.0f;
    image.Pixels = std::move(current);
}

float SrgbToLinear(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

Image Downsample(const Image &source, bool srgb, bool normalMap, TextureMipFilter filter)
{
    Image output;
    output.Width = std::max(source.Width / 2, 1u);
    output.Height = std::max(source.Height / 2, 1u);
    output.Hdr = source.Hdr;
    output.Pixels.resize(static_cast<size_t>(output.Width) * output.Height * 4);
    const int radius = filter == TextureMipFilter::Kaiser ? 2 : 1;
    for (uint32_t y = 0; y < output.Height; ++y)
        for (uint32_t x = 0; x < output.Width; ++x)
        {
            std::array<float, 4> sum{};
            float totalWeight = 0.0f;
            const float centerX = (static_cast<float>(x) + 0.5f) * static_cast<float>(source.Width) / output.Width;
            const float centerY = (static_cast<float>(y) + 0.5f) * static_cast<float>(source.Height) / output.Height;
            for (int offsetY = -radius; offsetY <= radius; ++offsetY)
                for (int offsetX = -radius; offsetX <= radius; ++offsetX)
                {
                    const int sx =
                        std::clamp(static_cast<int>(centerX) + offsetX, 0, static_cast<int>(source.Width) - 1);
                    const int sy =
                        std::clamp(static_cast<int>(centerY) + offsetY, 0, static_cast<int>(source.Height) - 1);
                    float weight = 1.0f;
                    if (filter == TextureMipFilter::Kaiser)
                    {
                        const float dx = std::abs((static_cast<float>(sx) + 0.5f) - centerX);
                        const float dy = std::abs((static_cast<float>(sy) + 0.5f) - centerY);
                        weight = std::max(0.0f, 1.0f - dx / 2.5f) * std::max(0.0f, 1.0f - dy / 2.5f);
                    }
                    const auto sample = Pixel(source, static_cast<uint32_t>(sx), static_cast<uint32_t>(sy));
                    for (size_t channel = 0; channel < 4; ++channel)
                    {
                        const float value = srgb && channel < 3 ? SrgbToLinear(sample[channel]) : sample[channel];
                        sum[channel] += value * weight;
                    }
                    totalWeight += weight;
                }
            const size_t destination = (static_cast<size_t>(y) * output.Width + x) * 4;
            for (size_t channel = 0; channel < 4; ++channel)
            {
                float value = sum[channel] / std::max(totalWeight, 0.0001f);
                if (srgb && channel < 3)
                    value = LinearToSrgb(value);
                output.Pixels[destination + channel] = value;
            }
            if (normalMap)
            {
                float nx = output.Pixels[destination] * 2.0f - 1.0f;
                float ny = output.Pixels[destination + 1] * 2.0f - 1.0f;
                float nz = output.Pixels[destination + 2] * 2.0f - 1.0f;
                const float inverseLength = 1.0f / std::max(std::sqrt(nx * nx + ny * ny + nz * nz), 0.0001f);
                output.Pixels[destination] = nx * inverseLength * 0.5f + 0.5f;
                output.Pixels[destination + 1] = ny * inverseLength * 0.5f + 0.5f;
                output.Pixels[destination + 2] = nz * inverseLength * 0.5f + 0.5f;
            }
        }
    return output;
}

float AlphaCoverage(const Image &image, float threshold)
{
    size_t covered = 0;
    for (size_t index = 3; index < image.Pixels.size(); index += 4)
        if (image.Pixels[index] >= threshold)
            ++covered;
    return static_cast<float>(covered) / static_cast<float>(image.Width * image.Height);
}

void PreserveCoverage(Image &mip, float targetCoverage, float threshold)
{
    float low = 0.0f, high = 8.0f;
    for (int iteration = 0; iteration < 12; ++iteration)
    {
        const float scale = (low + high) * 0.5f;
        size_t covered = 0;
        for (size_t index = 3; index < mip.Pixels.size(); index += 4)
            if (std::min(mip.Pixels[index] * scale, 1.0f) >= threshold)
                ++covered;
        const float coverage = static_cast<float>(covered) / static_cast<float>(mip.Width * mip.Height);
        if (coverage < targetCoverage)
            low = scale;
        else
            high = scale;
    }
    const float scale = (low + high) * 0.5f;
    for (size_t index = 3; index < mip.Pixels.size(); index += 4)
        mip.Pixels[index] = std::min(mip.Pixels[index] * scale, 1.0f);
}

std::vector<Image> BuildMips(const Image &base, const TextureAssetSettings &settings, bool srgb, bool normalMap)
{
    std::vector<Image> mips;
    mips.push_back(base);
    const uint32_t count = CalculateTextureMipCount(base.Width, base.Height, settings);
    const float coverage = settings.PreserveAlphaCoverage ? AlphaCoverage(base, settings.AlphaThreshold) : 0.0f;
    while (mips.size() < count)
    {
        Image next = Downsample(mips.back(), srgb, normalMap, settings.MipFilter);
        if (settings.PreserveAlphaCoverage)
            PreserveCoverage(next, coverage, settings.AlphaThreshold);
        mips.push_back(std::move(next));
    }
    return mips;
}

std::vector<std::byte> ImageBytes(const Image &image, RuntimeTextureFormat format)
{
    if (format == RuntimeTextureFormat::Rgba32Float)
    {
        std::vector<std::byte> bytes(image.Pixels.size() * sizeof(float));
        std::memcpy(bytes.data(), image.Pixels.data(), bytes.size());
        return bytes;
    }
    std::vector<std::byte> bytes(image.Pixels.size());
    for (size_t index = 0; index < image.Pixels.size(); ++index)
        bytes[index] = static_cast<std::byte>(
            static_cast<uint8_t>(std::round(std::clamp(image.Pixels[index], 0.0f, 1.0f) * 255.0f)));
    return bytes;
}

TexturePixelStorage PixelStorage(RuntimeTextureFormat format)
{
    switch (format)
    {
    case RuntimeTextureFormat::Rgba32Float:
        return TexturePixelStorage::Rgba32Float;
    case RuntimeTextureFormat::Bc1Unorm:
    case RuntimeTextureFormat::Bc1Srgb:
        return TexturePixelStorage::Bc1Rgb;
    case RuntimeTextureFormat::Bc3Unorm:
    case RuntimeTextureFormat::Bc3Srgb:
        return TexturePixelStorage::Bc3Rgba;
    case RuntimeTextureFormat::Bc5Unorm:
        return TexturePixelStorage::Bc5Rg;
    case RuntimeTextureFormat::Bc7Unorm:
    case RuntimeTextureFormat::Bc7Srgb:
        return TexturePixelStorage::Bc7Rgba;
    case RuntimeTextureFormat::Astc4x4Unorm:
    case RuntimeTextureFormat::Astc4x4Srgb:
        return TexturePixelStorage::Astc4x4Rgba;
    default:
        return TexturePixelStorage::Rgba8;
    }
}

cook::BlockCompressionQuality CompressionQuality(TextureCompressionQuality quality)
{
    switch (quality)
    {
    case TextureCompressionQuality::Fast:
        return cook::BlockCompressionQuality::Fast;
    case TextureCompressionQuality::High:
        return cook::BlockCompressionQuality::High;
    default:
        return cook::BlockCompressionQuality::Medium;
    }
}

bool HasAlpha(const Image &image)
{
    for (size_t index = 3; index < image.Pixels.size(); index += 4)
        if (image.Pixels[index] < 0.999f)
            return true;
    return false;
}

bool EncodeTexture(const std::vector<Image> &mips, RuntimeTextureFormat format, TextureUsage usage, bool srgb,
                   const TextureAssetSettings &settings, std::vector<std::byte> &output, std::string *error)
{
    resources::BinaryWriter writer;
    writer.WriteU32(3);
    writer.WriteU8(static_cast<uint8_t>(format));
    writer.WriteU8(static_cast<uint8_t>(usage));
    writer.WriteU8(srgb ? 1 : 0);
    writer.WriteU8(static_cast<uint8_t>(settings.Filter));
    writer.WriteU8(static_cast<uint8_t>(settings.AddressU));
    writer.WriteU8(static_cast<uint8_t>(settings.AddressV));
    writer.WriteU8(static_cast<uint8_t>(settings.AddressW));
    writer.WriteU8(0);
    writer.WriteF32(settings.MaximumAnisotropy);
    writer.WriteF32(settings.MipBias);
    writer.WriteU32(static_cast<uint32_t>(mips.size()));
    for (const Image &mip : mips)
    {
        std::vector<std::byte> bytes;
        const TexturePixelStorage storage = PixelStorage(format);
        if (IsBlockCompressed(storage))
        {
            const std::vector<std::byte> rgbaBytes = ImageBytes(mip, RuntimeTextureFormat::Rgba8Unorm);
            const auto rgba = std::span(reinterpret_cast<const uint8_t *>(rgbaBytes.data()), rgbaBytes.size());
            if (!cook::CompressTextureBlocks(storage, mip.Width, mip.Height, rgba,
                                             CompressionQuality(settings.Compression), srgb, bytes, error))
                return false;
        }
        else
            bytes = ImageBytes(mip, format);
        writer.WriteU32(mip.Width);
        writer.WriteU32(mip.Height);
        writer.WriteU64(bytes.size());
        writer.WriteBytes(bytes);
        if (IsBlockCompressed(storage))
        {
            const std::vector<std::byte> fallback = ImageBytes(mip, RuntimeTextureFormat::Rgba8Unorm);
            writer.WriteU64(fallback.size());
            writer.WriteBytes(fallback);
        }
        else
            writer.WriteU64(0);
    }
    output = writer.TakeData();
    return true;
}

std::shared_ptr<TextureData> LoadTextureResource(const resources::ResourceLoadContext &context, std::string *error)
{
    resources::BinaryReader reader(context.Payload);
    uint32_t version = 0, mipCount = 0;
    uint8_t formatValue = 0, usage = 0, srgb = 0, filter = 0, addressU = 0, addressV = 0, addressW = 0, reserved = 0;
    float anisotropy = 0.0f, mipBias = 0.0f;
    if (!reader.ReadU32(version) || (version < 1 || version > 3) || !reader.ReadU8(formatValue) || !reader.ReadU8(usage) ||
        !reader.ReadU8(srgb) || !reader.ReadU8(filter) || !reader.ReadU8(addressU) || !reader.ReadU8(addressV) ||
        !reader.ReadU8(addressW) || !reader.ReadU8(reserved) || !reader.ReadF32(anisotropy) ||
        !reader.ReadF32(mipBias) || !reader.ReadU32(mipCount) || mipCount == 0 || mipCount > 32 ||
        formatValue > static_cast<uint8_t>(version == 1 ? RuntimeTextureFormat::Rgba32Float
                                                        : RuntimeTextureFormat::Astc4x4Srgb) ||
        filter > static_cast<uint8_t>(TextureFilterMode::Anisotropic) ||
        addressU > static_cast<uint8_t>(TextureAddressMode::Border) ||
        addressV > static_cast<uint8_t>(TextureAddressMode::Border) ||
        addressW > static_cast<uint8_t>(TextureAddressMode::Border))
    {
        if (error)
            *error = reader.Error().empty() ? "Malformed texture resource header" : reader.Error();
        return {};
    }
    const auto format = static_cast<RuntimeTextureFormat>(formatValue);
    auto texture = std::make_shared<TextureData>();
    texture->SRGB = srgb != 0;
    texture->Storage = PixelStorage(format);
    texture->Filter = static_cast<TextureFilterMode>(filter);
    texture->AddressU = static_cast<TextureAddressMode>(addressU);
    texture->AddressV = static_cast<TextureAddressMode>(addressV);
    texture->AddressW = static_cast<TextureAddressMode>(addressW);
    texture->MaxAnisotropy = std::clamp(anisotropy, 1.0f, 16.0f);
    texture->MipBias = std::clamp(mipBias, -16.0f, 16.0f);
    for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
    {
        uint32_t width = 0, height = 0;
        uint64_t byteCount = 0;
        if (!reader.ReadU32(width) || !reader.ReadU32(height) || !reader.ReadU64(byteCount) || width == 0 ||
            height == 0 || width > 65536 || height > 65536)
        {
            if (error)
                *error = "Malformed texture mip dimensions";
            return {};
        }
        const uint64_t expectedSize = TextureMipByteSize(texture->Storage, width, height);
        if (byteCount != expectedSize || byteCount > reader.Remaining())
        {
            if (error)
                *error = "Malformed texture mip payload size";
            return {};
        }
        std::span<const std::byte> bytes;
        if (!reader.ReadBytes(static_cast<size_t>(byteCount), bytes))
        {
            if (error)
                *error = reader.Error();
            return {};
        }
        TextureMipData mip;
        mip.Width = static_cast<int>(width);
        mip.Height = static_cast<int>(height);
        if (texture->Storage == TexturePixelStorage::Rgba32Float)
        {
            mip.FloatPixels.resize(static_cast<size_t>(width) * height * 4);
            std::memcpy(mip.FloatPixels.data(), bytes.data(), bytes.size());
        }
        else
        {
            mip.Pixels.resize(bytes.size());
            std::memcpy(mip.Pixels.data(), bytes.data(), bytes.size());
        }
        texture->MipLevels.push_back(std::move(mip));
        if (version >= 3)
        {
            uint64_t fallbackByteCount = 0;
            if (!reader.ReadU64(fallbackByteCount) || fallbackByteCount > reader.Remaining())
            {
                if (error) *error = "Malformed texture fallback payload size";
                return {};
            }
            if (fallbackByteCount != 0)
            {
                const uint64_t expectedFallback = TextureMipByteSize(TexturePixelStorage::Rgba8,
                                                                     width, height);
                if (!IsBlockCompressed(texture->Storage) || fallbackByteCount != expectedFallback)
                {
                    if (error) *error = "Malformed RGBA8 texture fallback payload";
                    return {};
                }
                std::span<const std::byte> fallbackBytes;
                if (!reader.ReadBytes(static_cast<size_t>(fallbackByteCount), fallbackBytes))
                {
                    if (error) *error = reader.Error();
                    return {};
                }
                TextureMipData fallbackMip;
                fallbackMip.Width = static_cast<int>(width);
                fallbackMip.Height = static_cast<int>(height);
                fallbackMip.Pixels.resize(static_cast<size_t>(fallbackByteCount));
                std::memcpy(fallbackMip.Pixels.data(), fallbackBytes.data(), fallbackBytes.size());
                texture->Rgba8FallbackMipLevels.push_back(std::move(fallbackMip));
            }
        }
    }
    if (reader.Remaining() != 0)
    {
        if (error)
            *error = "Texture resource contains trailing data";
        return {};
    }
    texture->Width = texture->MipLevels.front().Width;
    texture->Height = texture->MipLevels.front().Height;
    texture->Pixels = texture->MipLevels.front().Pixels;
    texture->FloatPixels = texture->MipLevels.front().FloatPixels;
    (void)usage;
    return texture;
}

std::vector<AssetPropertySchema> TextureProperties()
{
    return {
        {"usage",
         "Usage",
         "Source",
         AssetPropertyType::Enumeration,
         "auto",
         {"auto", "color", "linear", "hdr", "normal", "normal_inverted", "height", "mask", "ui"}},
        {"color_space", "Color Space", "Source", AssetPropertyType::Enumeration, "auto", {"auto", "srgb", "linear"}},
        {"flip_vertical", "Flip Vertical", "Transform", AssetPropertyType::Boolean, "false"},
        {"flip_horizontal", "Flip Horizontal", "Transform", AssetPropertyType::Boolean, "false"},
        {"rotation", "Rotation", "Transform", AssetPropertyType::Enumeration, "0", {"0", "90", "180", "270"}},
        {"invert_green", "Invert Normal Green", "Transform", AssetPropertyType::Boolean, "false"},
        {"premultiply_alpha", "Premultiply Alpha", "Alpha", AssetPropertyType::Boolean, "false"},
        {"remove_alpha", "Remove Alpha", "Alpha", AssetPropertyType::Boolean, "false"},
        {"force_alpha", "Force Alpha", "Alpha", AssetPropertyType::Boolean, "false"},
        {"preserve_alpha_coverage", "Preserve Coverage", "Alpha", AssetPropertyType::Boolean, "false"},
        {"alpha_threshold", "Alpha Threshold", "Alpha", AssetPropertyType::Number, "0.5", {}, 0.0, 1.0},
        {"dilate_transparent", "Dilate Transparent Color", "Alpha", AssetPropertyType::Boolean, "true"},
        {"generate_mips", "Generate Mipmaps", "Mipmaps", AssetPropertyType::Boolean, "true"},
        {"mip_filter", "Mipmap Filter", "Mipmaps", AssetPropertyType::Enumeration, "kaiser", {"box", "kaiser"}},
        {"maximum_mip", "Maximum Mip", "Mipmaps", AssetPropertyType::Integer, "-1"},
        {"minimum_resolution", "Minimum Resolution", "Size", AssetPropertyType::Integer, "1", {}, 1.0, 65536.0},
        {"maximum_resolution", "Maximum Resolution", "Size", AssetPropertyType::Integer, "16384", {}, 1.0, 65536.0},
        {"downscale", "Downscale", "Size", AssetPropertyType::Integer, "1", {}, 1.0, 16.0},
        {"compression",
         "Compression Quality",
         "Compression",
         AssetPropertyType::Enumeration,
         "high",
         {"none", "fast", "medium", "high"}},
        {"pixel_format",
         "Pixel Format",
         "Compression",
         AssetPropertyType::Enumeration,
         "auto",
         {"auto", "rgba8", "rgba32f", "bc1", "bc3", "bc5", "bc7", "astc4x4"}},
        {"filter",
         "Filter",
         "Sampling",
         AssetPropertyType::Enumeration,
         "anisotropic",
         {"nearest", "bilinear", "trilinear", "anisotropic"}},
        {"address_u",
         "Address U",
         "Sampling",
         AssetPropertyType::Enumeration,
         "repeat",
         {"repeat", "clamp", "mirror", "border"}},
        {"address_v",
         "Address V",
         "Sampling",
         AssetPropertyType::Enumeration,
         "repeat",
         {"repeat", "clamp", "mirror", "border"}},
        {"max_anisotropy", "Maximum Anisotropy", "Sampling", AssetPropertyType::Number, "8", {}, 1.0, 16.0},
        {"mip_bias", "Mip Bias", "Sampling", AssetPropertyType::Number, "0", {}, -16.0, 16.0},
    };
}

} // namespace

TextureUsage DetectTextureUsage(const std::filesystem::path &path, bool hdr)
{
    if (hdr)
        return TextureUsage::Hdr;
    const std::string name = Lower(path.stem().generic_string());
    auto contains = [&](std::string_view token) { return name.find(token) != std::string::npos; };
    if (contains("normal") || contains("_nrm") || contains("_nor"))
        return TextureUsage::NormalMap;
    if (contains("rough") || contains("metal") || contains("occlusion") || contains("_ao") || contains("mrao") ||
        contains("orm"))
        return TextureUsage::LinearData;
    if (contains("height") || contains("bump") || contains("disp"))
        return TextureUsage::HeightMap;
    if (contains("mask") || contains("opacity"))
        return TextureUsage::Mask;
    if (contains("sprite") || contains("ui_"))
        return TextureUsage::UiSprite;
    return TextureUsage::Color;
}

RuntimeTextureFormat ChooseTextureFormat(const TextureAssetSettings &settings, bool hasAlpha, bool hdr,
                                         std::string_view platform, std::vector<AssetDiagnostic> *diagnostics)
{
    if (hdr || settings.Usage == TextureUsage::Hdr)
    {
        if (settings.PreferredPixelFormat != "auto" && settings.PreferredPixelFormat != "rgba32f" && diagnostics)
            diagnostics->push_back(MakeDiagnostic(
                AssetDiagnosticSeverity::Warning, "hdr_block_format_unsupported",
                "The selected LDR block format cannot preserve HDR values; RGBA32F was selected instead.", {},
                "Use rgba32f until the target platform enables BC6H or ASTC HDR."));
        return RuntimeTextureFormat::Rgba32Float;
    }
    const bool srgb = settings.ColorSpace == TextureColorSpace::Srgb ||
                      (settings.ColorSpace == TextureColorSpace::Auto &&
                       (settings.Usage == TextureUsage::Color || settings.Usage == TextureUsage::UiSprite ||
                        settings.Usage == TextureUsage::Auto));
    const std::string preferred = Lower(settings.PreferredPixelFormat);
    if (preferred == "rgba8" || settings.Compression == TextureCompressionQuality::None)
        return srgb ? RuntimeTextureFormat::Rgba8Srgb : RuntimeTextureFormat::Rgba8Unorm;
    if (preferred == "rgba32f")
        return RuntimeTextureFormat::Rgba32Float;
    if (preferred == "bc1")
        return srgb ? RuntimeTextureFormat::Bc1Srgb : RuntimeTextureFormat::Bc1Unorm;
    if (preferred == "bc3")
        return srgb ? RuntimeTextureFormat::Bc3Srgb : RuntimeTextureFormat::Bc3Unorm;
    if (preferred == "bc5")
        return RuntimeTextureFormat::Bc5Unorm;
    if (preferred == "bc7")
        return srgb ? RuntimeTextureFormat::Bc7Srgb : RuntimeTextureFormat::Bc7Unorm;
    if (preferred == "astc4x4")
        return srgb ? RuntimeTextureFormat::Astc4x4Srgb : RuntimeTextureFormat::Astc4x4Unorm;

    const std::string platformName = Lower(std::string(platform));
    const bool mobile = platformName.find("mobile") != std::string::npos ||
                        platformName.find("android") != std::string::npos ||
                        platformName.find("ios") != std::string::npos;
    if (mobile)
        return srgb ? RuntimeTextureFormat::Astc4x4Srgb : RuntimeTextureFormat::Astc4x4Unorm;
    if (settings.Usage == TextureUsage::NormalMap || settings.Usage == TextureUsage::InvertedNormalMap)
        return RuntimeTextureFormat::Bc5Unorm;
    if (settings.Compression == TextureCompressionQuality::High)
        return srgb ? RuntimeTextureFormat::Bc7Srgb : RuntimeTextureFormat::Bc7Unorm;
    if (hasAlpha)
        return srgb ? RuntimeTextureFormat::Bc3Srgb : RuntimeTextureFormat::Bc3Unorm;
    return srgb ? RuntimeTextureFormat::Bc1Srgb : RuntimeTextureFormat::Bc1Unorm;
}

uint32_t CalculateTextureMipCount(uint32_t width, uint32_t height, const TextureAssetSettings &settings)
{
    if (!settings.GenerateMips)
        return 1;
    uint32_t count = 1;
    while (width > 1 || height > 1)
    {
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
        ++count;
        if (settings.MaximumMipLevel >= 0 && count >= static_cast<uint32_t>(settings.MaximumMipLevel + 1))
            break;
    }
    return count;
}

bool ValidateTextureSettings(const TextureAssetSettings &settings, std::vector<AssetDiagnostic> &diagnostics)
{
    if (settings.MinimumResolution == 0 || settings.MaximumResolution == 0 ||
        settings.MinimumResolution > settings.MaximumResolution || settings.MaximumResolution > 65536)
        diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "invalid_texture_resolution",
                                             "Texture min/max resolution is invalid."));
    if (settings.Downscale == 0 || settings.Downscale > 16)
        diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "invalid_texture_downscale",
                                             "Texture downscale must be between 1 and 16."));
    if (settings.AlphaThreshold < 0.0f || settings.AlphaThreshold > 1.0f)
        diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "invalid_alpha_threshold",
                                             "Alpha threshold must be in [0, 1]."));
    if (settings.MaximumAnisotropy < 1.0f || settings.MaximumAnisotropy > 16.0f)
        diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "invalid_anisotropy",
                                             "Maximum anisotropy must be in [1, 16]."));
    if (settings.MipBias < -16.0f || settings.MipBias > 16.0f)
        diagnostics.push_back(
            MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "invalid_mip_bias", "Mip bias must be in [-16, 16]."));
    static constexpr std::array<std::string_view, 8> formats = {"auto", "rgba8", "rgba32f", "bc1",
                                                                "bc3", "bc5",  "bc7",     "astc4x4"};
    const std::string preferredFormat = Lower(settings.PreferredPixelFormat);
    if (std::find(formats.begin(), formats.end(), preferredFormat) == formats.end())
        diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "invalid_texture_format",
                                             "Unknown preferred texture format: " + settings.PreferredPixelFormat));
    for (const std::string &swizzle : settings.Swizzle)
        if (ChannelIndex(swizzle) < 0 && swizzle != "black" && swizzle != "white" && swizzle != "zero" &&
            swizzle != "one" && swizzle != "0" && swizzle != "1")
            diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "invalid_channel_swizzle",
                                                 "Invalid channel swizzle: " + swizzle));
    return !std::any_of(diagnostics.begin(), diagnostics.end(), [](const AssetDiagnostic &diagnostic) {
        return diagnostic.Severity == AssetDiagnosticSeverity::FatalError;
    });
}

TextureAssetSettings ReadTextureAssetSettings(const AssetDescriptor &descriptor, std::string_view profile)
{
    TextureAssetSettings settings;
    const auto values = descriptor.ResolveSettings(profile);
    auto get = [&](std::string_view key, std::string_view fallback) {
        const auto found = values.find(std::string(key));
        return found == values.end() ? std::string(fallback) : found->second;
    };
    ParseEnum(get("usage", "auto"), settings.Usage);
    ParseEnum(get("color_space", "auto"), settings.ColorSpace);
    ParseBool(get("flip_vertical", "false"), settings.FlipVertical);
    ParseBool(get("flip_horizontal", "false"), settings.FlipHorizontal);
    ParseEnum(get("rotation", "0"), settings.Rotation);
    ParseBool(get("invert_green", "false"), settings.InvertGreen);
    ParseBool(get("premultiply_alpha", "false"), settings.PremultiplyAlpha);
    ParseBool(get("remove_alpha", "false"), settings.RemoveAlpha);
    ParseBool(get("force_alpha", "false"), settings.ForceAlpha);
    ParseBool(get("bump_to_normal", "false"), settings.BumpToNormal);
    ParseFloat(get("bump_strength", "1"), settings.BumpStrength);
    for (size_t index = 0; index < 4; ++index)
    {
        settings.Swizzle[index] = get("swizzle_" + std::to_string(index), settings.Swizzle[index]);
        settings.ChannelSources[index] = get("channel_source_" + std::to_string(index), settings.ChannelSources[index]);
    }
    ParseBool(get("generate_mips", "true"), settings.GenerateMips);
    ParseEnum(get("mip_filter", "kaiser"), settings.MipFilter);
    ParseBool(get("preserve_alpha_coverage", "false"), settings.PreserveAlphaCoverage);
    ParseFloat(get("alpha_threshold", "0.5"), settings.AlphaThreshold);
    ParseInteger(get("maximum_mip", "-1"), settings.MaximumMipLevel);
    ParseInteger(get("minimum_resolution", "1"), settings.MinimumResolution);
    ParseInteger(get("maximum_resolution", "16384"), settings.MaximumResolution);
    ParseInteger(get("downscale", "1"), settings.Downscale);
    ParseEnum(get("compression", "high"), settings.Compression);
    settings.PreferredPixelFormat = get("pixel_format", "auto");
    ParseEnum(get("filter", "anisotropic"), settings.Filter);
    ParseEnum(get("address_u", "repeat"), settings.AddressU);
    ParseEnum(get("address_v", "repeat"), settings.AddressV);
    ParseEnum(get("address_w", "repeat"), settings.AddressW);
    ParseFloat(get("max_anisotropy", "8"), settings.MaximumAnisotropy);
    ParseFloat(get("mip_bias", "0"), settings.MipBias);
    ParseBool(get("dilate_transparent", "true"), settings.DilateTransparentColor);
    return settings;
}

void WriteTextureAssetSettings(AssetDescriptor &descriptor, const TextureAssetSettings &settings)
{
    auto &values = descriptor.Settings;
    values["usage"] = ToString(settings.Usage);
    values["color_space"] = ToString(settings.ColorSpace);
    values["flip_vertical"] = Bool(settings.FlipVertical);
    values["flip_horizontal"] = Bool(settings.FlipHorizontal);
    values["rotation"] = ToString(settings.Rotation);
    values["invert_green"] = Bool(settings.InvertGreen);
    values["premultiply_alpha"] = Bool(settings.PremultiplyAlpha);
    values["remove_alpha"] = Bool(settings.RemoveAlpha);
    values["force_alpha"] = Bool(settings.ForceAlpha);
    values["bump_to_normal"] = Bool(settings.BumpToNormal);
    values["bump_strength"] = Float(settings.BumpStrength);
    for (size_t index = 0; index < 4; ++index)
    {
        values["swizzle_" + std::to_string(index)] = settings.Swizzle[index];
        values["channel_source_" + std::to_string(index)] = settings.ChannelSources[index];
    }
    values["generate_mips"] = Bool(settings.GenerateMips);
    values["mip_filter"] = ToString(settings.MipFilter);
    values["preserve_alpha_coverage"] = Bool(settings.PreserveAlphaCoverage);
    values["alpha_threshold"] = Float(settings.AlphaThreshold);
    values["maximum_mip"] = std::to_string(settings.MaximumMipLevel);
    values["minimum_resolution"] = std::to_string(settings.MinimumResolution);
    values["maximum_resolution"] = std::to_string(settings.MaximumResolution);
    values["downscale"] = std::to_string(settings.Downscale);
    values["compression"] = ToString(settings.Compression);
    values["pixel_format"] = settings.PreferredPixelFormat;
    values["filter"] = ToString(settings.Filter);
    values["address_u"] = ToString(settings.AddressU);
    values["address_v"] = ToString(settings.AddressV);
    values["address_w"] = ToString(settings.AddressW);
    values["max_anisotropy"] = Float(settings.MaximumAnisotropy);
    values["mip_bias"] = Float(settings.MipBias);
    values["dilate_transparent"] = Bool(settings.DilateTransparentColor);
}

bool RegisterTextureAssetType(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                              std::string *error)
{
    AssetTypeRegistration type;
    type.TypeId = std::string(kTexture2DAssetType);
    type.DisplayName = "Texture 2D";
    type.DescriptorExtension = std::string(kTexture2DDescriptorExtension);
    type.SourceExtensions = {"png", "jpg", "jpeg", "tga", "bmp", "psd", "gif", "pic", "pnm", "ppm", "pgm", "hdr"};
    type.ImportModes = {{"auto", "Automatic texture usage", 100}};
    type.RuntimeType = std::string(kTexture2DResourceType);
    type.Icon = "texture-2d";
    type.DescriptorVersion = 1;
    type.ImporterVersion = 1;
    // v3 adds an RGBA8 safety mip chain next to BC/ASTC payloads. Bumping both
    // versions invalidates old cache entries so unsupported GPUs never depend
    // on a legacy compressed-only resource.
    type.TransformerVersion = 3;
    type.ResourceVersion = 3;
    type.Properties = TextureProperties();
    type.Import = [](const AssetImportRequest &request) {
        ImportResult result;
        std::string relativeError;
        const std::filesystem::path relative =
            NormalizeProjectRelative(request.ProjectRoot, request.SourcePath, &relativeError);
        if (relative.empty())
        {
            result.Diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "source_outside_project",
                                                        relativeError, request.SourcePath,
                                                        "Move the source into the project before importing."));
            return result;
        }
        ImportedAsset imported;
        imported.DescriptorPath = request.DestinationDirectory / (request.SourcePath.stem().generic_string() + "." +
                                                                  std::string(kTexture2DDescriptorExtension));
        if (std::filesystem::is_regular_file(imported.DescriptorPath))
        {
            std::string loadError;
            if (!LoadAssetDescriptor(imported.DescriptorPath, imported.Descriptor, &loadError))
            {
                result.Diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError,
                                                            "existing_descriptor_invalid", loadError,
                                                            imported.DescriptorPath));
                return result;
            }
            imported.Reused = true;
            result.ReusedAssets.push_back(std::move(imported));
            result.Succeeded = true;
            return result;
        }

        int width = 0, height = 0, channels = 0;
        bool hdr = false;
        {
            std::scoped_lock lock(g_stbiMutex);
            hdr = stbi_is_hdr(request.SourcePath.string().c_str()) != 0;
            if (stbi_info(request.SourcePath.string().c_str(), &width, &height, &channels) == 0)
            {
                result.Diagnostics.push_back(
                    MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "image_probe_failed",
                                   "Cannot read image metadata: " +
                                       std::string(stbi_failure_reason() ? stbi_failure_reason() : "unknown error"),
                                   request.SourcePath));
                return result;
            }
        }
        AssetDescriptor &descriptor = imported.Descriptor;
        descriptor.Guid = AssetGuid::Generate();
        descriptor.Type = std::string(kTexture2DAssetType);
        descriptor.Name = request.SourcePath.stem().generic_string();
        descriptor.Sources = {relative};
        descriptor.SourceDependencies = {relative};
        descriptor.State = AssetImportState::Dirty;
        TextureAssetSettings settings;
        settings.Usage = DetectTextureUsage(request.SourcePath, hdr);
        settings.ColorSpace = (settings.Usage == TextureUsage::Color || settings.Usage == TextureUsage::UiSprite)
                                  ? TextureColorSpace::Srgb
                                  : TextureColorSpace::Linear;
        settings.PreserveAlphaCoverage = settings.Usage == TextureUsage::Mask;
        settings.InvertGreen = settings.Usage == TextureUsage::InvertedNormalMap;
        WriteTextureAssetSettings(descriptor, settings);
        descriptor.UserMetadata["source.width"] = std::to_string(width);
        descriptor.UserMetadata["source.height"] = std::to_string(height);
        descriptor.UserMetadata["source.channels"] = std::to_string(channels);
        descriptor.UserMetadata["source.hdr"] = Bool(hdr);
        result.Statistics["source_pixels"] = static_cast<uint64_t>(width) * height;
        result.GeneratedAssets.push_back(std::move(imported));
        result.Succeeded = true;
        return result;
    };
    type.Validate = [](const AssetDescriptor &descriptor, const AssetValidationContext &context) {
        std::vector<AssetDiagnostic> diagnostics;
        if (descriptor.Sources.empty() || descriptor.Sources.size() > 4)
            diagnostics.push_back(MakeDiagnostic(AssetDiagnosticSeverity::FatalError, "invalid_texture_source_count",
                                                 "Texture requires between one and four source images.",
                                                 context.DescriptorPath));
        ValidateTextureSettings(ReadTextureAssetSettings(descriptor, context.Profile), diagnostics);
        return diagnostics;
    };
    type.Transform = [](const AssetDescriptor &descriptor, const AssetTransformContext &context,
                        AssetTransformOutput &output, std::string *transformError) {
        AssetDescriptor resolvedDescriptor = descriptor;
        resolvedDescriptor.Settings = context.ResolvedSettings;
        resolvedDescriptor.PlatformOverrides.clear();
        TextureAssetSettings settings = ReadTextureAssetSettings(resolvedDescriptor);
        std::vector<Image> sources;
        for (const auto &source : descriptor.Sources)
        {
            Image image;
            const std::filesystem::path absolute = source.is_absolute() ? source : context.ProjectRoot / source;
            if (!DecodeImage(absolute, image, transformError))
                return false;
            sources.push_back(std::move(image));
        }
        Image image;
        if (!PackChannels(sources, settings, image, transformError))
            return false;
        TextureUsage usage = settings.Usage == TextureUsage::Auto
                                 ? DetectTextureUsage(descriptor.Sources.front(), image.Hdr)
                                 : settings.Usage;
        if (usage == TextureUsage::InvertedNormalMap)
            settings.InvertGreen = true;
        if (usage == TextureUsage::HeightMap && settings.BumpToNormal)
        {
            ConvertBumpToNormal(image, settings.BumpStrength);
            usage = TextureUsage::NormalMap;
        }
        bool validSwizzle = true;
        ApplySwizzle(image, settings.Swizzle, validSwizzle);
        if (!validSwizzle)
        {
            if (transformError)
                *transformError = "Invalid channel swizzle";
            return false;
        }
        Flip(image, settings.FlipHorizontal, settings.FlipVertical);
        Rotate(image, settings.Rotation);
        for (size_t index = 0; index < image.Pixels.size(); index += 4)
        {
            if (settings.InvertGreen)
                image.Pixels[index + 1] = 1.0f - image.Pixels[index + 1];
            if (settings.PremultiplyAlpha)
                for (size_t channel = 0; channel < 3; ++channel)
                    image.Pixels[index + channel] *= image.Pixels[index + 3];
            if (settings.RemoveAlpha || settings.ForceAlpha)
                image.Pixels[index + 3] = 1.0f;
        }
        if (settings.DilateTransparentColor && !image.Hdr)
            DilateTransparent(image);

        const uint32_t largest = std::max(image.Width, image.Height);
        uint32_t targetLargest = std::max(largest / std::max(settings.Downscale, 1u), 1u);
        targetLargest = std::clamp(targetLargest, settings.MinimumResolution, settings.MaximumResolution);
        if (targetLargest != largest)
        {
            const float scale = static_cast<float>(targetLargest) / static_cast<float>(largest);
            image = Resize(image, std::max(static_cast<uint32_t>(std::round(image.Width * scale)), 1u),
                           std::max(static_cast<uint32_t>(std::round(image.Height * scale)), 1u));
        }
        const bool srgb = settings.ColorSpace == TextureColorSpace::Srgb ||
                          (settings.ColorSpace == TextureColorSpace::Auto &&
                           (usage == TextureUsage::Color || usage == TextureUsage::UiSprite));
        const bool normalMap = usage == TextureUsage::NormalMap || usage == TextureUsage::InvertedNormalMap;
        const bool alpha = HasAlpha(image);
        const RuntimeTextureFormat format =
            ChooseTextureFormat(settings, alpha, image.Hdr, context.Platform + "/" + context.Profile,
                                &output.Diagnostics);
        const std::vector<Image> mips = BuildMips(image, settings, srgb, normalMap);
        output.RuntimeType = std::string(kTexture2DResourceType);
        output.ResourceVersion = 2;
        output.Compression = settings.Compression == TextureCompressionQuality::None ? "none" : "rle";
        if (!EncodeTexture(mips, format, usage, srgb, settings, output.Payload, transformError))
            return false;
        output.Statistics["width"] = image.Width;
        output.Statistics["height"] = image.Height;
        output.Statistics["mip_levels"] = mips.size();
        output.Statistics["payload_bytes"] = output.Payload.size();
        if (alpha)
            output.Diagnostics.push_back(
                MakeDiagnostic(AssetDiagnosticSeverity::Info, "alpha_detected",
                               settings.PreserveAlphaCoverage
                                   ? "Alpha detected; mask/alpha-coverage workflow is enabled."
                                   : "Alpha detected; opaque, mask or blend material mode may be appropriate."));
        return true;
    };
    type.Preview = [](const AssetDescriptor &descriptor, const AssetPreviewRequest &request, AssetPreview &preview,
                      std::string *previewError) {
        if (descriptor.Sources.empty())
        {
            if (previewError)
                *previewError = "Texture has no source";
            return false;
        }
        Image image;
        const auto path = descriptor.Sources.front().is_absolute() ? descriptor.Sources.front()
                                                                   : request.ProjectRoot / descriptor.Sources.front();
        if (!DecodeImage(path, image, previewError))
            return false;
        const float scale = std::min({1.0f, static_cast<float>(request.MaximumWidth) / image.Width,
                                      static_cast<float>(request.MaximumHeight) / image.Height});
        image = Resize(image, std::max(static_cast<uint32_t>(image.Width * scale), 1u),
                       std::max(static_cast<uint32_t>(image.Height * scale), 1u));
        preview.Kind = "texture2d";
        preview.MimeType = image.Hdr ? "application/x-rgba32f" : "application/x-rgba8";
        preview.Width = image.Width;
        preview.Height = image.Height;
        preview.Bytes =
            ImageBytes(image, image.Hdr ? RuntimeTextureFormat::Rgba32Float : RuntimeTextureFormat::Rgba8Unorm);
        preview.Metadata["usage"] = ToString(ReadTextureAssetSettings(descriptor).Usage);
        preview.Metadata["source"] = descriptor.Sources.front().generic_string();
        preview.CacheKey = descriptor.LastTransformFingerprint.IsValid() ? descriptor.LastTransformFingerprint
                                                                         : HashFile(path, nullptr);
        return true;
    };
    type.Inspector = [](const AssetDescriptor &) { return TextureProperties(); };
    type.ProfileDefaults = [](std::string_view profile) {
        std::map<std::string, std::string> defaults;
        if (profile == "mobile")
        {
            defaults["maximum_resolution"] = "2048";
            defaults["compression"] = "high";
            defaults["pixel_format"] = "astc4x4";
            defaults["max_anisotropy"] = "4";
        }
        else if (profile == "low")
        {
            defaults["maximum_resolution"] = "1024";
            defaults["downscale"] = "2";
            defaults["compression"] = "fast";
        }
        return defaults;
    };
    if (!registry.Register(std::move(type), error))
        return false;
    if (!resourceManager.RegisterLoader<TextureData>(
            std::string(kTexture2DResourceType),
            [](const resources::ResourceLoadContext &context, std::string *loadError) {
                return LoadTextureResource(context, loadError);
            },
            error))
    {
        registry.Unregister(kTexture2DAssetType);
        return false;
    }
    return true;
}

} // namespace engine::assets
