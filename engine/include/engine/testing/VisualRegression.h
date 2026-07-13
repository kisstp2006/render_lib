#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::testing {

// RGB pixels are always stored in linear light. LDR inputs are decoded from
// sRGB during loading, while Radiance HDR inputs retain their linear values.
struct VisualImage
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool SourceWasHdr = false;
    std::vector<float> LinearRgb;

    bool Valid() const;
};

struct VisualTolerance
{
    // A channel passes when:
    // abs(reference - candidate) <= Absolute + Relative *
    // max(|ref|, |candidate|, RelativeFloor).
    // This mixed tolerance remains useful in both dark and high-radiance areas.
    float Absolute = 0.015f;
    float Relative = 0.04f;
    float RelativeFloor = 0.05f;
    float MaximumFailingPixelFraction = 0.01f;
    float MaximumMeanNormalizedError = 0.20f;
    float DifferenceVisualizationGain = 8.0f;
};

struct VisualMetrics
{
    uint64_t PixelCount = 0;
    uint64_t FailingPixelCount = 0;
    double FailingPixelFraction = 0.0;
    double MeanAbsoluteError = 0.0;
    double MeanRelativeError = 0.0;
    double RootMeanSquareError = 0.0;
    double MaximumAbsoluteError = 0.0;
    double MeanNormalizedError = 0.0;
    double PeakSignal = 1.0;
    double PsnrDecibels = 0.0;
    bool Passed = false;
};

struct VisualComparison
{
    VisualMetrics Metrics;
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::vector<uint8_t> DifferenceRgba;
    std::vector<uint8_t> HeatmapRgba;
    std::string Error;

    bool Valid() const { return Error.empty() && Width > 0 && Height > 0; }
};

bool LoadVisualImage(const std::filesystem::path& path, VisualImage& image,
                     std::string* error = nullptr);
bool WriteHdrImage(const std::filesystem::path& path, uint32_t width, uint32_t height,
                   const std::vector<float>& linearRgb, std::string* error = nullptr);
bool WriteRgbaImage(const std::filesystem::path& path, uint32_t width, uint32_t height,
                    const std::vector<uint8_t>& rgba, std::string* error = nullptr);

VisualComparison CompareVisualImages(const VisualImage& reference,
                                     const VisualImage& candidate,
                                     const VisualTolerance& tolerance = {});

bool WriteVisualComparisonReport(const std::filesystem::path& path,
                                 const std::filesystem::path& referencePath,
                                 const std::filesystem::path& candidatePath,
                                 const VisualTolerance& tolerance,
                                 const VisualComparison& comparison,
                                 std::string* error = nullptr);

} // namespace engine::testing
