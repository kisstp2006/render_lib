#include "engine/asset/ColorGrading.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace engine::color_grading {
namespace {

[[noreturn]] void Fail(const std::string& path, int line, const std::string& message)
{
    throw std::runtime_error("Color LUT '" + path + "'" + (line > 0 ? ":" + std::to_string(line) : "")
                             + ": " + message);
}

glm::vec3 ReadVec3(std::istringstream& stream, const std::string& path, int line, const char* label)
{
    glm::vec3 value{};
    if (!(stream >> value.x >> value.y >> value.z)
        || !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
        Fail(path, line, std::string("invalid ") + label + " value");
    return value;
}

std::shared_ptr<ColorGradingLutData> Generate(int size, bool cinematic)
{
    if (size < 2 || size > 128)
        throw std::runtime_error("Generated color LUT size must be between 2 and 128");

    auto lut = std::make_shared<ColorGradingLutData>();
    lut->Size = size;
    lut->SourcePath = cinematic ? "<built-in cinematic>" : "<identity>";
    lut->Values.reserve(static_cast<size_t>(size) * size * size);
    const float denominator = static_cast<float>(size - 1);
    for (int blue = 0; blue < size; ++blue)
    {
        for (int green = 0; green < size; ++green)
        {
            for (int red = 0; red < size; ++red)
            {
                glm::vec3 color{red / denominator, green / denominator, blue / denominator};
                if (cinematic)
                {
                    // A restrained Source-style teal-shadow / warm-highlight
                    // look used by the sandbox to make LUT blending obvious.
                    const float luma = glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
                    const float shadow = 1.0f - glm::smoothstep(0.12f, 0.58f, luma);
                    const float highlight = glm::smoothstep(0.45f, 0.95f, luma);
                    color += shadow * glm::vec3(-0.020f, 0.018f, 0.045f);
                    color += highlight * glm::vec3(0.040f, 0.012f, -0.018f);
                    color = (color - 0.5f) * 1.08f + 0.5f;
                    color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                }
                lut->Values.push_back(color);
            }
        }
    }
    return lut;
}

} // namespace

std::shared_ptr<ColorGradingLutData> LoadCube(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
        Fail(path, 0, "could not open file");

    auto lut = std::make_shared<ColorGradingLutData>();
    lut->SourcePath = path;
    std::string lineText;
    int lineNumber = 0;
    while (std::getline(file, lineText))
    {
        ++lineNumber;
        if (const size_t comment = lineText.find('#'); comment != std::string::npos)
            lineText.erase(comment);
        std::istringstream stream(lineText);
        std::string token;
        if (!(stream >> token))
            continue;

        if (token == "TITLE")
            continue;
        if (token == "LUT_1D_SIZE")
            Fail(path, lineNumber, "1D LUTs are not supported; expected LUT_3D_SIZE");
        if (token == "LUT_3D_SIZE")
        {
            if (lut->Size != 0)
                Fail(path, lineNumber, "duplicate LUT_3D_SIZE");
            if (!(stream >> lut->Size) || lut->Size < 2 || lut->Size > 128)
                Fail(path, lineNumber, "LUT_3D_SIZE must be between 2 and 128");
            continue;
        }
        if (token == "DOMAIN_MIN")
        {
            lut->DomainMin = ReadVec3(stream, path, lineNumber, "DOMAIN_MIN");
            continue;
        }
        if (token == "DOMAIN_MAX")
        {
            lut->DomainMax = ReadVec3(stream, path, lineNumber, "DOMAIN_MAX");
            continue;
        }
        if (token == "LUT_3D_INPUT_RANGE")
        {
            float minimum = 0.0f;
            float maximum = 1.0f;
            if (!(stream >> minimum >> maximum) || !std::isfinite(minimum) || !std::isfinite(maximum)
                || maximum <= minimum)
                Fail(path, lineNumber, "invalid LUT_3D_INPUT_RANGE");
            lut->DomainMin = glm::vec3(minimum);
            lut->DomainMax = glm::vec3(maximum);
            continue;
        }

        stream.clear();
        stream.str(lineText);
        lut->Values.push_back(ReadVec3(stream, path, lineNumber, "RGB sample"));
    }

    if (lut->Size == 0)
        Fail(path, 0, "missing LUT_3D_SIZE");
    const size_t expected = static_cast<size_t>(lut->Size) * lut->Size * lut->Size;
    if (lut->Values.size() != expected)
        Fail(path, 0, "expected " + std::to_string(expected) + " RGB samples, found "
                      + std::to_string(lut->Values.size()));
    if (glm::any(glm::lessThanEqual(lut->DomainMax, lut->DomainMin)))
        Fail(path, 0, "DOMAIN_MAX must be greater than DOMAIN_MIN on every axis");
    return lut;
}

std::shared_ptr<ColorGradingLutData> MakeIdentity(int size)
{
    return Generate(size, false);
}

std::shared_ptr<ColorGradingLutData> MakeCinematic(int size)
{
    return Generate(size, true);
}

} // namespace engine::color_grading
