#include "engine/core/Camera.h"
#include "engine/render/CascadedShadows.h"
#include "engine/scene/Texture.h"
#include "engine/scene/Environment.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <glm/gtc/epsilon.hpp>

using namespace engine;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

bool MatrixNear(const glm::mat4& a, const glm::mat4& b, float epsilon)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (std::abs(a[column][row] - b[column][row]) > epsilon)
                return false;
    return true;
}

void TestSplitDistribution()
{
    const auto linear = CalculateCascadeSplits(1.0f, 101.0f, 0.0f);
    Require(std::abs(linear[0] - 26.0f) < 0.001f, "lambda=0 must produce linear splits");
    const auto logarithmic = CalculateCascadeSplits(1.0f, 10000.0f, 1.0f);
    Require(std::abs(logarithmic[0] - 10.0f) < 0.001f, "lambda=1 must produce logarithmic splits");
    Require(std::abs(logarithmic[3] - 10000.0f) < 0.001f, "last split must equal shadow distance");
    for (int i = 1; i < kShadowCascadeCount; ++i)
        Require(logarithmic[i] > logarithmic[i - 1], "splits must be strictly increasing");
}

void TestCoverageAndResolution()
{
    Camera camera;
    camera.Position = {3.0f, 5.0f, 7.0f};
    camera.Yaw = -72.0f;
    camera.Pitch = -12.0f;
    camera.NearPlane = 0.1f;
    camera.FarPlane = 500.0f;
    CascadeShadowConfig config;
    const CascadeShadowData cascades = BuildCascadeShadows(camera, 16.0f / 9.0f, {-0.4f, -0.8f, -0.3f}, config);

    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade)
    {
        Require(cascades.SplitDepths[cascade] > cascades.NearDepths[cascade], "cascade depth interval must be positive");
        Require(cascades.WorldUnitsPerTexel[cascade] > 0.0f, "texel world size must be positive");
        if (cascade > 0)
        {
            Require(cascades.NearDepths[cascade] < cascades.SplitDepths[cascade - 1],
                    "adjacent cascades must overlap for transition blending");
            Require(cascades.WorldUnitsPerTexel[cascade] > cascades.WorldUnitsPerTexel[cascade - 1],
                    "far cascades must have lower effective world resolution");
        }

        const float middle = (cascades.NearDepths[cascade] + cascades.SplitDepths[cascade]) * 0.5f;
        const glm::vec4 clip = cascades.LightMatrices[cascade] * glm::vec4(camera.Position + camera.Forward() * middle, 1.0f);
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        Require(std::abs(ndc.x) <= 1.001f && std::abs(ndc.y) <= 1.001f && std::abs(ndc.z) <= 1.001f,
                "cascade center must be covered by its light projection");

        const glm::vec3 forward = camera.Forward();
        const glm::vec3 right = camera.Right();
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));
        const float tanHalfFov = std::tan(glm::radians(camera.FovDegrees) * 0.5f);
        for (float depth : {cascades.NearDepths[cascade], cascades.SplitDepths[cascade]})
        {
            const float halfHeight = tanHalfFov * depth;
            const float halfWidth = halfHeight * (16.0f / 9.0f);
            const glm::vec3 center = camera.Position + forward * depth;
            for (float x : {-1.0f, 1.0f})
            {
                for (float y : {-1.0f, 1.0f})
                {
                    const glm::vec3 corner = center + right * halfWidth * x + up * halfHeight * y;
                    const glm::vec4 cornerClip = cascades.LightMatrices[cascade] * glm::vec4(corner, 1.0f);
                    const glm::vec3 cornerNdc = glm::vec3(cornerClip) / cornerClip.w;
                    Require(std::abs(cornerNdc.x) <= 1.001f && std::abs(cornerNdc.y) <= 1.001f
                                && std::abs(cornerNdc.z) <= 1.001f,
                            "every cascade frustum corner must be covered by its light projection");
                }
            }
        }
    }
}

void TestTexelStabilization()
{
    Camera camera;
    camera.Position = {0.0f, 2.0f, 5.0f};
    CascadeShadowConfig config;
    const glm::vec3 lightDirection = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.3f));
    const CascadeShadowData first = BuildCascadeShadows(camera, 16.0f / 9.0f, lightDirection, config);

    const glm::vec3 referenceUp = std::abs(lightDirection.y) > 0.98f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 lightRight = glm::normalize(glm::cross(lightDirection, referenceUp));
    camera.Position += lightRight * first.WorldUnitsPerTexel[0] * 0.2f;
    const CascadeShadowData second = BuildCascadeShadows(camera, 16.0f / 9.0f, lightDirection, config);
    Require(MatrixNear(first.LightMatrices[0], second.LightMatrices[0], 1e-5f),
            "sub-texel lateral camera motion must not move the nearest shadow projection");
}

void TestProceduralLightCookie()
{
    const auto cookie = textures::MakeLightCookie(64);
    Require(cookie && cookie->Width == 64 && cookie->Height == 64, "procedural cookie must have the requested dimensions");
    const auto sample = [&](int x, int y) { return cookie->Pixels[(static_cast<size_t>(y) * cookie->Width + x) * 4]; };
    Require(sample(32, 32) > 100, "cookie center must transmit light");
    Require(sample(0, 0) == 0, "cookie corners must block light");
}

void TestHdrEnvironmentLoadingAndCache()
{
    const std::string path = std::string(TEST_ASSET_DIR) + "/studio_small_09_1k.hdr";
    const auto first = environments::LoadHdrFromFile(path);
    const auto second = environments::LoadHdrFromFile(path);
    Require(first != nullptr, "HDR environment must load");
    Require(first == second, "repeated HDR loads must reuse the CPU cache");
    Require(first->Width == 1024 && first->Height == 512 && first->Channels == 3,
            "HDR environment dimensions and channels must be preserved");
    Require(first->Pixels.size() == static_cast<size_t>(first->Width) * first->Height * 3,
            "HDR environment must contain tightly packed RGB floats");
    bool hasHdrValue = false;
    for (float value : first->Pixels)
    {
        Require(std::isfinite(value) && value >= 0.0f, "HDR pixels must be finite positive linear radiance");
        hasHdrValue = hasHdrValue || value > 1.0f;
    }
    Require(hasHdrValue, "HDR loader must preserve radiance above the LDR range");

    bool missingFileReported = false;
    try
    {
        environments::LoadHdrFromFile(std::string(TEST_ASSET_DIR) + "/does-not-exist.hdr");
    }
    catch (const EnvironmentLoadError& error)
    {
        missingFileReported = std::string(error.what()).find("does not exist") != std::string::npos;
    }
    Require(missingFileReported, "missing HDR assets must report a useful error");

    bool invalidFileReported = false;
    try
    {
        environments::LoadHdrFromFile(std::string(TEST_ASSET_DIR) + "/WaterBottle.glb");
    }
    catch (const EnvironmentLoadError& error)
    {
        invalidFileReported = std::string(error.what()).find("not a valid Radiance") != std::string::npos;
    }
    Require(invalidFileReported, "non-HDR assets must report their invalid format");
}

void TestDayNightTransitions()
{
    const DayNightState day = EvaluateDayNight(30.0f);
    Require(day.DayAmount > 0.999f && day.NightAmount < 0.001f,
            "high sun elevation must produce daylight");
    Require(day.DirectSunAmount > 0.999f, "daylight must keep direct sun lighting enabled");

    const DayNightState sunset = EvaluateDayNight(0.0f);
    Require(sunset.TwilightAmount > 0.8f, "sunset must strongly activate twilight scattering");
    Require(sunset.DirectSunAmount > 0.0f && sunset.DirectSunAmount < 1.0f,
            "direct sunlight must fade continuously around the horizon");
    Require(sunset.SunTint.g < day.SunTint.g, "low sun must become warmer than midday sun");

    const DayNightState nauticalTwilight = EvaluateDayNight(-12.0f);
    Require(nauticalTwilight.NightAmount > 0.0f && nauticalTwilight.NightAmount < 1.0f,
            "nautical twilight must transition toward night");
    Require(nauticalTwilight.DirectSunAmount < 0.001f,
            "sun below the horizon must not provide direct lighting");

    const DayNightState night = EvaluateDayNight(-20.0f);
    Require(night.NightAmount > 0.999f && night.DayAmount < 0.001f,
            "sun below astronomical twilight must produce full night");
}

} // namespace

int main()
{
    TestSplitDistribution();
    TestCoverageAndResolution();
    TestTexelStabilization();
    TestProceduralLightCookie();
    TestHdrEnvironmentLoadingAndCache();
    TestDayNightTransitions();
    std::puts("Cascaded shadow tests passed");
    return EXIT_SUCCESS;
}
