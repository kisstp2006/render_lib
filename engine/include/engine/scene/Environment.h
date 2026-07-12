#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

// CPU-side, backend-independent linear HDR panorama data.
struct HdrImageData
{
    int Width = 0;
    int Height = 0;
    int Channels = 3;
    std::vector<float> Pixels;
    std::string SourcePath;
};

class EnvironmentLoadError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

namespace environments {

// Loads a Radiance RGBE .hdr panorama as linear floating-point RGB. Repeated
// loads of the same normalized path reuse the CPU image while it is alive.
std::shared_ptr<HdrImageData> LoadHdrFromFile(const std::string& path, bool flipVertically = false);

} // namespace environments

enum class EnvironmentSource
{
    ProceduralSky,
    EquirectangularHdr,
};

struct EnvironmentSettings
{
    EnvironmentSource Source = EnvironmentSource::ProceduralSky;
    std::shared_ptr<HdrImageData> Hdri;

    // Source 2-style exposure controls, expressed in photographic stops.
    // ExposureEV participates in the IBL bake; BackgroundExposureEV only
    // changes the visible sky and therefore does not trigger a re-bake.
    float ExposureEV = 0.0f;
    float BackgroundExposureEV = 0.0f;
    float RotationDegrees = 0.0f; // yaw around the engine's +Y up axis
};

struct DayNightState
{
    float SunElevationDegrees = 0.0f;
    float DayAmount = 1.0f;
    float TwilightAmount = 0.0f;
    float NightAmount = 0.0f;
    float DirectSunAmount = 1.0f;
    glm::vec3 SunTint{1.0f};
};

// Uses the standard civil/nautical/astronomical twilight bands (-6/-12/-18
// degrees) to derive stable renderer controls from the solar elevation.
DayNightState EvaluateDayNight(float sunElevationDegrees);

} // namespace engine
