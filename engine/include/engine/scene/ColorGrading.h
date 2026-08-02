#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

// Backend-independent CPU representation of an industry-standard 3D color
// grading LUT. It is renderer scene data, not an asset-pipeline dependency.
struct ColorGradingLutData
{
    int Size = 0;
    glm::vec3 DomainMin{0.0f};
    glm::vec3 DomainMax{1.0f};
    std::vector<glm::vec3> Values;
    std::string SourcePath;
};

namespace color_grading {

std::shared_ptr<ColorGradingLutData> LoadCube(const std::string& path);
std::shared_ptr<ColorGradingLutData> MakeIdentity(int size = 32);
std::shared_ptr<ColorGradingLutData> MakeCinematic(int size = 32);

} // namespace color_grading
} // namespace engine
