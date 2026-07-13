#include "engine/testing/VisualRegression.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

float Number(const char* value, const char* option)
{
    char* end = nullptr;
    const float result = std::strtof(value, &end);
    if (!end || *end != '\0')
        throw std::runtime_error(std::string("Invalid value for ") + option + ": " + value);
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    using namespace engine::testing;
    if (argc < 3)
    {
        std::cerr << "Usage: engine_visual_compare <reference> <candidate> "
                     "[--diff out.png] [--heatmap out.png] [--report out.json] "
                     "[--absolute N] [--relative N] [--relative-floor N] "
                     "[--max-failing-fraction N] [--max-mean-normalized N]\n";
        return 2;
    }

    const std::filesystem::path referencePath = argv[1];
    const std::filesystem::path candidatePath = argv[2];
    std::filesystem::path diffPath;
    std::filesystem::path heatmapPath;
    std::filesystem::path reportPath;
    VisualTolerance tolerance;
    try
    {
        for (int index = 3; index < argc; ++index)
        {
            const std::string option = argv[index];
            auto value = [&]() -> const char* {
                if (++index >= argc)
                    throw std::runtime_error("Missing value for " + option);
                return argv[index];
            };
            if (option == "--diff") diffPath = value();
            else if (option == "--heatmap") heatmapPath = value();
            else if (option == "--report") reportPath = value();
            else if (option == "--absolute") tolerance.Absolute = Number(value(), option.c_str());
            else if (option == "--relative") tolerance.Relative = Number(value(), option.c_str());
            else if (option == "--relative-floor") tolerance.RelativeFloor = Number(value(), option.c_str());
            else if (option == "--max-failing-fraction")
                tolerance.MaximumFailingPixelFraction = Number(value(), option.c_str());
            else if (option == "--max-mean-normalized")
                tolerance.MaximumMeanNormalizedError = Number(value(), option.c_str());
            else if (option == "--diff-gain")
                tolerance.DifferenceVisualizationGain = Number(value(), option.c_str());
            else throw std::runtime_error("Unknown option: " + option);
        }
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 2;
    }

    VisualImage reference;
    VisualImage candidate;
    std::string error;
    if (!LoadVisualImage(referencePath, reference, &error) ||
        !LoadVisualImage(candidatePath, candidate, &error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    const VisualComparison comparison = CompareVisualImages(reference, candidate, tolerance);
    if (!comparison.Valid())
    {
        std::cerr << comparison.Error << '\n';
        return 2;
    }
    if (!diffPath.empty() &&
        !WriteRgbaImage(diffPath, comparison.Width, comparison.Height,
                        comparison.DifferenceRgba, &error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    if (!heatmapPath.empty() &&
        !WriteRgbaImage(heatmapPath, comparison.Width, comparison.Height,
                        comparison.HeatmapRgba, &error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    if (!reportPath.empty() &&
        !WriteVisualComparisonReport(reportPath, referencePath, candidatePath,
                                     tolerance, comparison, &error))
    {
        std::cerr << error << '\n';
        return 2;
    }

    const VisualMetrics& metrics = comparison.Metrics;
    std::cout << (metrics.Passed ? "PASS" : "FAIL")
              << " mean_abs=" << metrics.MeanAbsoluteError
              << " mean_rel=" << metrics.MeanRelativeError
              << " mean_norm=" << metrics.MeanNormalizedError
              << " failing=" << metrics.FailingPixelCount << "/" << metrics.PixelCount
              << " (" << metrics.FailingPixelFraction * 100.0 << "%)"
              << " psnr=" << metrics.PsnrDecibels << " dB\n";
    return metrics.Passed ? 0 : 1;
}
