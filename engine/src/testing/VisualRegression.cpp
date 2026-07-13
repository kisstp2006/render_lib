#include "engine/testing/VisualRegression.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>

namespace engine::testing {
namespace {

float SrgbToLinear(float value)
{
    return value <= 0.04045f ? value / 12.92f
                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f ? value * 12.92f
                              : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

uint8_t Byte(float value)
{
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

bool PrepareParent(const std::filesystem::path& path, std::string* error)
{
    if (!path.has_parent_path())
        return true;
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (!directoryError)
        return true;
    if (error)
        *error = "Cannot create output directory: " + directoryError.message();
    return false;
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream stream;
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '\\': stream << "\\\\"; break;
        case '"': stream << "\\\""; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (character < 0x20)
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(character) << std::dec;
            else
                stream << static_cast<char>(character);
        }
    }
    return stream.str();
}

struct Color
{
    float R, G, B;
};

Color Mix(Color left, Color right, float t)
{
    return {left.R + (right.R - left.R) * t,
            left.G + (right.G - left.G) * t,
            left.B + (right.B - left.B) * t};
}

Color HeatColor(float normalizedError)
{
    // 0: black, <1: blue/cyan, 1: yellow (tolerance boundary), >1: red/white.
    const float t = std::clamp(normalizedError * 0.5f, 0.0f, 1.0f);
    constexpr Color stops[] = {
        {0.0f, 0.0f, 0.0f}, {0.05f, 0.10f, 0.45f}, {0.0f, 0.75f, 1.0f},
        {1.0f, 0.95f, 0.05f}, {1.0f, 0.05f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    constexpr int intervalCount = static_cast<int>(std::size(stops)) - 1;
    const float scaled = t * intervalCount;
    const int interval = std::min(static_cast<int>(scaled), intervalCount - 1);
    return Mix(stops[interval], stops[interval + 1], scaled - interval);
}

} // namespace

bool VisualImage::Valid() const
{
    return Width > 0 && Height > 0 &&
           LinearRgb.size() == static_cast<size_t>(Width) * Height * 3;
}

bool LoadVisualImage(const std::filesystem::path& path, VisualImage& image,
                     std::string* error)
{
    image = {};
    const std::string nativePath = path.string();
    if (!std::filesystem::is_regular_file(path))
    {
        if (error) *error = "Image does not exist: " + nativePath;
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    if (stbi_is_hdr(nativePath.c_str()))
    {
        float* pixels = stbi_loadf(nativePath.c_str(), &width, &height, &channels, 3);
        if (!pixels)
        {
            if (error) *error = "Cannot decode HDR image: " + nativePath;
            return false;
        }
        image.SourceWasHdr = true;
        image.LinearRgb.assign(pixels, pixels + static_cast<size_t>(width) * height * 3);
        stbi_image_free(pixels);
    }
    else
    {
        uint8_t* pixels = stbi_load(nativePath.c_str(), &width, &height, &channels, 3);
        if (!pixels)
        {
            if (error) *error = "Cannot decode image: " + nativePath;
            return false;
        }
        image.LinearRgb.resize(static_cast<size_t>(width) * height * 3);
        for (size_t index = 0; index < image.LinearRgb.size(); ++index)
            image.LinearRgb[index] = SrgbToLinear(static_cast<float>(pixels[index]) / 255.0f);
        stbi_image_free(pixels);
    }
    if (width <= 0 || height <= 0)
    {
        if (error) *error = "Decoded image has invalid dimensions: " + nativePath;
        image = {};
        return false;
    }
    image.Width = static_cast<uint32_t>(width);
    image.Height = static_cast<uint32_t>(height);
    for (const float value : image.LinearRgb)
    {
        if (!std::isfinite(value))
        {
            if (error) *error = "Image contains a non-finite channel: " + nativePath;
            image = {};
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

bool WriteHdrImage(const std::filesystem::path& path, uint32_t width, uint32_t height,
                   const std::vector<float>& linearRgb, std::string* error)
{
    if (width == 0 || height == 0 ||
        linearRgb.size() != static_cast<size_t>(width) * height * 3)
    {
        if (error) *error = "HDR output has invalid dimensions or pixel count";
        return false;
    }
    if (!PrepareParent(path, error))
        return false;
    stbi_flip_vertically_on_write(0);
    if (stbi_write_hdr(path.string().c_str(), static_cast<int>(width),
                       static_cast<int>(height), 3, linearRgb.data()) == 0)
    {
        if (error) *error = "Failed to write HDR image: " + path.string();
        return false;
    }
    if (error) error->clear();
    return true;
}

bool WriteRgbaImage(const std::filesystem::path& path, uint32_t width, uint32_t height,
                    const std::vector<uint8_t>& rgba, std::string* error)
{
    if (width == 0 || height == 0 ||
        rgba.size() != static_cast<size_t>(width) * height * 4)
    {
        if (error) *error = "PNG output has invalid dimensions or pixel count";
        return false;
    }
    if (!PrepareParent(path, error))
        return false;
    stbi_flip_vertically_on_write(0);
    if (stbi_write_png(path.string().c_str(), static_cast<int>(width),
                       static_cast<int>(height), 4, rgba.data(),
                       static_cast<int>(width * 4)) == 0)
    {
        if (error) *error = "Failed to write PNG image: " + path.string();
        return false;
    }
    if (error) error->clear();
    return true;
}

VisualComparison CompareVisualImages(const VisualImage& reference,
                                     const VisualImage& candidate,
                                     const VisualTolerance& tolerance)
{
    VisualComparison result;
    if (!reference.Valid() || !candidate.Valid())
    {
        result.Error = "Reference or candidate image is invalid";
        return result;
    }
    if (reference.Width != candidate.Width || reference.Height != candidate.Height)
    {
        result.Error = "Image dimensions differ: reference " +
            std::to_string(reference.Width) + "x" + std::to_string(reference.Height) +
            ", candidate " + std::to_string(candidate.Width) + "x" +
            std::to_string(candidate.Height);
        return result;
    }
    if (tolerance.Absolute < 0.0f || tolerance.Relative < 0.0f ||
        tolerance.RelativeFloor <= 0.0f ||
        tolerance.MaximumFailingPixelFraction < 0.0f ||
        tolerance.MaximumMeanNormalizedError < 0.0f)
    {
        result.Error = "Visual comparison tolerance contains an invalid negative value";
        return result;
    }

    result.Width = reference.Width;
    result.Height = reference.Height;
    const size_t pixelCount = static_cast<size_t>(result.Width) * result.Height;
    result.DifferenceRgba.resize(pixelCount * 4);
    result.HeatmapRgba.resize(pixelCount * 4);
    result.Metrics.PixelCount = pixelCount;
    double absoluteSum = 0.0;
    double relativeSum = 0.0;
    double squaredSum = 0.0;
    double normalizedSum = 0.0;

    for (size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        float pixelNormalizedError = 0.0f;
        bool pixelFailed = false;
        for (size_t channel = 0; channel < 3; ++channel)
        {
            const size_t index = pixel * 3 + channel;
            const float referenceValue = reference.LinearRgb[index];
            const float candidateValue = candidate.LinearRgb[index];
            const float difference = std::abs(referenceValue - candidateValue);
            const float signal = std::max(std::abs(referenceValue), std::abs(candidateValue));
            const float allowed = tolerance.Absolute + tolerance.Relative *
                std::max(signal, tolerance.RelativeFloor);
            const float normalized = allowed > 0.0f
                ? difference / allowed
                : (difference == 0.0f ? 0.0f : std::numeric_limits<float>::infinity());
            pixelNormalizedError = std::max(pixelNormalizedError, normalized);
            pixelFailed = pixelFailed || normalized > 1.0f;
            absoluteSum += difference;
            relativeSum += difference / std::max(signal, tolerance.RelativeFloor);
            squaredSum += static_cast<double>(difference) * difference;
            result.Metrics.MaximumAbsoluteError =
                std::max(result.Metrics.MaximumAbsoluteError, static_cast<double>(difference));
            result.Metrics.PeakSignal =
                std::max(result.Metrics.PeakSignal, static_cast<double>(signal));

            const float visualized = 1.0f - std::exp(
                -difference * std::max(tolerance.DifferenceVisualizationGain, 0.0f));
            result.DifferenceRgba[pixel * 4 + channel] = Byte(LinearToSrgb(visualized));
        }
        result.DifferenceRgba[pixel * 4 + 3] = 255;
        if (pixelFailed)
            ++result.Metrics.FailingPixelCount;
        normalizedSum += std::min(static_cast<double>(pixelNormalizedError), 1000.0);
        const Color heat = HeatColor(pixelNormalizedError);
        result.HeatmapRgba[pixel * 4 + 0] = Byte(heat.R);
        result.HeatmapRgba[pixel * 4 + 1] = Byte(heat.G);
        result.HeatmapRgba[pixel * 4 + 2] = Byte(heat.B);
        result.HeatmapRgba[pixel * 4 + 3] = 255;
    }

    const double valueCount = static_cast<double>(pixelCount * 3);
    result.Metrics.MeanAbsoluteError = absoluteSum / valueCount;
    result.Metrics.MeanRelativeError = relativeSum / valueCount;
    result.Metrics.RootMeanSquareError = std::sqrt(squaredSum / valueCount);
    result.Metrics.MeanNormalizedError = normalizedSum / static_cast<double>(pixelCount);
    result.Metrics.FailingPixelFraction = static_cast<double>(result.Metrics.FailingPixelCount) /
                                          static_cast<double>(pixelCount);
    result.Metrics.PsnrDecibels = result.Metrics.RootMeanSquareError > 0.0
        ? 20.0 * std::log10(result.Metrics.PeakSignal /
                            result.Metrics.RootMeanSquareError)
        : std::numeric_limits<double>::infinity();
    result.Metrics.Passed =
        result.Metrics.FailingPixelFraction <= tolerance.MaximumFailingPixelFraction &&
        result.Metrics.MeanNormalizedError <= tolerance.MaximumMeanNormalizedError;
    return result;
}

bool WriteVisualComparisonReport(const std::filesystem::path& path,
                                 const std::filesystem::path& referencePath,
                                 const std::filesystem::path& candidatePath,
                                 const VisualTolerance& tolerance,
                                 const VisualComparison& comparison,
                                 std::string* error)
{
    if (!PrepareParent(path, error))
        return false;
    std::ofstream file(path, std::ios::trunc);
    if (!file)
    {
        if (error) *error = "Cannot open JSON report: " + path.string();
        return false;
    }
    const VisualMetrics& metrics = comparison.Metrics;
    file << std::setprecision(10)
         << "{\n"
         << "  \"reference\": \"" << JsonEscape(referencePath.generic_string()) << "\",\n"
         << "  \"candidate\": \"" << JsonEscape(candidatePath.generic_string()) << "\",\n"
         << "  \"passed\": " << (metrics.Passed ? "true" : "false") << ",\n"
         << "  \"error\": \"" << JsonEscape(comparison.Error) << "\",\n"
         << "  \"width\": " << comparison.Width << ",\n"
         << "  \"height\": " << comparison.Height << ",\n"
         << "  \"tolerance\": {\n"
         << "    \"absolute\": " << tolerance.Absolute << ",\n"
         << "    \"relative\": " << tolerance.Relative << ",\n"
         << "    \"relativeFloor\": " << tolerance.RelativeFloor << ",\n"
         << "    \"maximumFailingPixelFraction\": "
         << tolerance.MaximumFailingPixelFraction << ",\n"
         << "    \"maximumMeanNormalizedError\": "
         << tolerance.MaximumMeanNormalizedError << "\n"
         << "  },\n"
         << "  \"metrics\": {\n"
         << "    \"pixelCount\": " << metrics.PixelCount << ",\n"
         << "    \"failingPixelCount\": " << metrics.FailingPixelCount << ",\n"
         << "    \"failingPixelFraction\": " << metrics.FailingPixelFraction << ",\n"
         << "    \"meanAbsoluteError\": " << metrics.MeanAbsoluteError << ",\n"
         << "    \"meanRelativeError\": " << metrics.MeanRelativeError << ",\n"
         << "    \"rootMeanSquareError\": " << metrics.RootMeanSquareError << ",\n"
         << "    \"maximumAbsoluteError\": " << metrics.MaximumAbsoluteError << ",\n"
         << "    \"meanNormalizedError\": " << metrics.MeanNormalizedError << ",\n"
         << "    \"peakSignal\": " << metrics.PeakSignal << ",\n"
         << "    \"psnrDecibels\": ";
    if (std::isfinite(metrics.PsnrDecibels)) file << metrics.PsnrDecibels;
    else file << "null";
    file << "\n  }\n}\n";
    if (!file)
    {
        if (error) *error = "Failed while writing JSON report: " + path.string();
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace engine::testing
