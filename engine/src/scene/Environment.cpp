#include "engine/scene/Environment.h"
#include "engine/core/Log.h"
#include "engine/profiling/MemoryProfiler.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace engine::environments {

namespace {

std::mutex g_cacheMutex;
std::unordered_map<std::string, std::weak_ptr<HdrImageData>> g_hdrCache;

std::string NormalizePath(const std::string& path)
{
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? std::filesystem::path(path) : absolute).lexically_normal().generic_string();
}

} // namespace

std::shared_ptr<HdrImageData> LoadHdrFromFile(const std::string& path, bool flipVertically)
{
    ENGINE_MEMORY_TAG_SCOPE("Asset");
    if (path.empty())
        throw EnvironmentLoadError("HDR environment path is empty");

    const std::string normalizedPath = NormalizePath(path);
    const std::string cacheKey = normalizedPath + (flipVertically ? "|flip" : "|native");
    {
        std::scoped_lock lock(g_cacheMutex);
        if (const auto found = g_hdrCache.find(cacheKey); found != g_hdrCache.end())
            if (auto cached = found->second.lock())
                return cached;
    }

    if (!std::filesystem::is_regular_file(normalizedPath))
        throw EnvironmentLoadError("HDR environment file does not exist: " + normalizedPath);
    if (!stbi_is_hdr(normalizedPath.c_str()))
        throw EnvironmentLoadError("Environment is not a valid Radiance .hdr image: " + normalizedPath);

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    float* decoded = stbi_loadf(normalizedPath.c_str(), &width, &height, &sourceChannels, 3);
    if (!decoded)
        throw EnvironmentLoadError("Failed to decode HDR environment '" + normalizedPath + "': "
                                   + (stbi_failure_reason() ? stbi_failure_reason() : "unknown stb_image error"));

    if (width < 4 || height < 2)
    {
        stbi_image_free(decoded);
        throw EnvironmentLoadError("HDR environment is too small (minimum 4x2): " + normalizedPath);
    }

    auto image = std::make_shared<HdrImageData>();
    image->Width = width;
    image->Height = height;
    image->SourcePath = normalizedPath;
    const size_t valueCount = static_cast<size_t>(width) * height * 3;
    image->Pixels.assign(decoded, decoded + valueCount);
    stbi_image_free(decoded);

    for (float& value : image->Pixels)
    {
        if (!std::isfinite(value))
            throw EnvironmentLoadError("HDR environment contains a non-finite pixel value: " + normalizedPath);
        value = std::max(value, 0.0f); // Source 2's sky path also clamps to positive.
    }

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (std::abs(aspect - 2.0f) > 0.1f)
        log::Warn("HDR environment is not a 2:1 equirectangular panorama ("
                  + std::to_string(width) + "x" + std::to_string(height) + "): " + normalizedPath);

    {
        std::scoped_lock lock(g_cacheMutex);
        g_hdrCache[cacheKey] = image;
    }
    log::Info("Loaded linear HDR environment: " + normalizedPath + " ("
              + std::to_string(width) + "x" + std::to_string(height) + ")");
    return image;
}

} // namespace engine::environments

namespace engine {

namespace {

float Smoothstep(float edge0, float edge1, float value)
{
    const float t = glm::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

DayNightState EvaluateDayNight(float sunElevationDegrees)
{
    DayNightState state;
    state.SunElevationDegrees = sunElevationDegrees;
    state.DayAmount = Smoothstep(-6.0f, 6.0f, sunElevationDegrees);
    state.NightAmount = 1.0f - Smoothstep(-18.0f, -6.0f, sunElevationDegrees);
    state.TwilightAmount = Smoothstep(-18.0f, -6.0f, sunElevationDegrees)
                          * (1.0f - Smoothstep(2.0f, 12.0f, sunElevationDegrees));
    state.DirectSunAmount = Smoothstep(-2.0f, 3.0f, sunElevationDegrees);
    const float daylightTint = Smoothstep(-2.0f, 15.0f, sunElevationDegrees);
    state.SunTint = glm::mix(glm::vec3(1.0f, 0.30f, 0.08f), glm::vec3(1.0f, 0.97f, 0.90f), daylightTint);
    return state;
}

} // namespace engine
