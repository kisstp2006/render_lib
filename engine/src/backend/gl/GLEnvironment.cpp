#include "engine/backend/gl/GLEnvironment.h"
#include "engine/backend/gl/GLDebug.h"
#include "engine/core/Log.h"
#include "engine/profiling/CpuProfiler.h"

#include <glad/gl.h>

#include <chrono>
#include <cmath>

namespace engine {

namespace {

constexpr int kEnvSize = 256;
constexpr int kIrradianceSize = 32;
constexpr int kPrefilterSize = 128;
constexpr int kBrdfLutSize = 512;

// Per-face (right, up, forward) bases matching the GL cubemap face
// definitions (sc/tc/ma from the spec), so that a fullscreen triangle with
// dir = right*ndc.x + up*ndc.y + forward lands each texel on the correct
// cube direction.
struct FaceBasis
{
    glm::vec3 Right, Up, Forward;
};

const FaceBasis kFaces[6] = {
    {{0, 0, -1}, {0, -1, 0}, {1, 0, 0}},   // +X
    {{0, 0, 1}, {0, -1, 0}, {-1, 0, 0}},   // -X
    {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},     // +Y
    {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}},   // -Y
    {{1, 0, 0}, {0, -1, 0}, {0, 0, 1}},    // +Z
    {{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}},  // -Z
};

glm::mat3 FaceBasisMatrix(int face)
{
    const FaceBasis& f = kFaces[face];
    return glm::mat3(f.Right, f.Up, f.Forward);
}

bool SameSky(const SkySettings& a, const SkySettings& b)
{
    return a.ZenithColor == b.ZenithColor
        && a.HorizonColor == b.HorizonColor
        && a.GroundColor == b.GroundColor
        && a.NightZenithColor == b.NightZenithColor
        && a.NightHorizonColor == b.NightHorizonColor
        && a.NightSkyIntensity == b.NightSkyIntensity
        && a.NightHorizonGlow == b.NightHorizonGlow
        && a.SunAngularRadiusDeg == b.SunAngularRadiusDeg
        && a.SunIntensity == b.SunIntensity
        && a.SkyIntensity == b.SkyIntensity
        && a.EnableDayNightCycle == b.EnableDayNightCycle;
}

unsigned int CreateCubemap(int size, bool mipmapped)
{
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    for (int face = 0; face < 6; ++face)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);

    if (mipmapped)
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    return tex;
}

} // namespace

GLEnvironment::GLEnvironment(const std::string& shaderDir)
{
    const std::string fullscreen = shaderDir + "/gl/common/fullscreen.vert";
    const std::string environment = shaderDir + "/gl/environment/";
    m_skyGenShader = std::make_unique<GLShader>(fullscreen, environment + "sky_gen.frag");
    m_equirectShader = std::make_unique<GLShader>(fullscreen, environment + "equirect_to_cube.frag");
    m_irradianceShader = std::make_unique<GLShader>(fullscreen, environment + "irradiance.frag");
    m_prefilterShader = std::make_unique<GLShader>(fullscreen, environment + "prefilter.frag");
    m_brdfShader = std::make_unique<GLShader>(fullscreen, environment + "brdf_lut.frag");

    glGenFramebuffers(1, &m_fbo);
    glGenVertexArrays(1, &m_emptyVao);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    gl_debug::LabelObject(GL_FRAMEBUFFER, m_fbo, "IBL Bake FBO");
    glBindVertexArray(m_emptyVao);
    gl_debug::LabelObject(GL_VERTEX_ARRAY, m_emptyVao, "IBL Fullscreen Triangle VAO");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);

    m_envCubemap = CreateCubemap(kEnvSize, true);
    m_irradianceCubemap = CreateCubemap(kIrradianceSize, false);
    m_prefilterCubemap = CreateCubemap(kPrefilterSize, true);
    gl_debug::LabelObject(GL_TEXTURE, m_envCubemap, "IBL Environment Cubemap");
    gl_debug::LabelObject(GL_TEXTURE, m_irradianceCubemap, "IBL Irradiance Cubemap");
    gl_debug::LabelObject(GL_TEXTURE, m_prefilterCubemap, "IBL GGX Prefilter Cubemap");

    // BRDF LUT is sun/sky independent: bake it once here.
    glGenTextures(1, &m_brdfLut);
    glBindTexture(GL_TEXTURE_2D, m_brdfLut);
    gl_debug::LabelObject(GL_TEXTURE, m_brdfLut, "IBL BRDF LUT");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, kBrdfLutSize, kBrdfLutSize, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_brdfLut, 0);
    glViewport(0, 0, kBrdfLutSize, kBrdfLutSize);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_emptyVao);
    m_brdfShader->Use();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    log::Info("IBL: BRDF LUT baked (" + std::to_string(kBrdfLutSize) + "px)");
}

GLEnvironment::~GLEnvironment()
{
    for (const auto& [image, texture] : m_panoramaCache)
    {
        (void)image;
        glDeleteTextures(1, &texture);
    }
    glDeleteTextures(1, &m_envCubemap);
    glDeleteTextures(1, &m_irradianceCubemap);
    glDeleteTextures(1, &m_prefilterCubemap);
    glDeleteTextures(1, &m_brdfLut);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteVertexArrays(1, &m_emptyVao);
}

void GLEnvironment::EnsureBaked(const DirectionalLight& sun, const SkySettings& sky, const EnvironmentSettings& environment)
{
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Environment.Check", "Renderer/OpenGL/IBL");
    const bool sourceChanged = !m_lastBake.Valid || m_lastBake.Source != environment.Source;
    const bool sunDirectionChanged = environment.Source == EnvironmentSource::ProceduralSky
                                  && m_lastBake.SunDir != sun.Direction;
    const bool proceduralSettingsChanged = environment.Source == EnvironmentSource::ProceduralSky
        && (m_lastBake.SunColor != sun.Color
            || m_lastBake.SunIntensity != sun.Intensity
            || !SameSky(m_lastBake.Sky, sky));
    const bool hdriChanged = environment.Source == EnvironmentSource::EquirectangularHdr
        && (m_lastBake.Hdri != environment.Hdri.get()
            || m_lastBake.ExposureEV != environment.ExposureEV
            || m_lastBake.RotationDegrees != environment.RotationDegrees);
    const bool dirty = sourceChanged || sunDirectionChanged || proceduralSettingsChanged || hdriChanged;

    if (!dirty)
        return;

    // A moving sun updates the screen-resolution disk, direct lighting and
    // shadows every frame. Throttle only the expensive derived IBL bake; any
    // source or artist-setting change remains immediate.
    const bool directionOnly = m_lastBake.Valid && !sourceChanged && sunDirectionChanged
                            && !proceduralSettingsChanged && !hdriChanged;
    const auto now = std::chrono::steady_clock::now();
    if (directionOnly && now - m_lastBakeTime < std::chrono::milliseconds(350))
        return;

    if (environment.Source == EnvironmentSource::EquirectangularHdr && !environment.Hdri)
        throw EnvironmentLoadError("Equirectangular HDR environment selected, but no HDR image was assigned");

    Bake(sun, sky, environment, directionOnly);

    m_lastBake.SunDir = sun.Direction;
    m_lastBake.SunColor = sun.Color;
    m_lastBake.SunIntensity = sun.Intensity;
    m_lastBake.Sky = sky;
    m_lastBake.Source = environment.Source;
    m_lastBake.Hdri = environment.Hdri.get();
    m_lastBake.ExposureEV = environment.ExposureEV;
    m_lastBake.RotationDegrees = environment.RotationDegrees;
    m_lastBake.Valid = true;
    m_lastBakeTime = std::chrono::steady_clock::now();
}

unsigned int GLEnvironment::GetOrCreatePanorama(const std::shared_ptr<HdrImageData>& image)
{
    if (const auto found = m_panoramaCache.find(image); found != m_panoramaCache.end())
        return found->second;

    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    gl_debug::LabelObject(GL_TEXTURE, texture, "HDRI Equirectangular Source");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, image->Width, image->Height, 0, GL_RGB, GL_FLOAT, image->Pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_panoramaCache.emplace(image, texture);
    return texture;
}

void GLEnvironment::Bake(const DirectionalLight& sun, const SkySettings& sky,
                         const EnvironmentSettings& environment, bool fastUpdate)
{
    gl_debug::ScopedGroup marker("Environment / IBL Bake");
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Environment.Bake", "Renderer/OpenGL/IBL");
    const auto bakeStart = std::chrono::steady_clock::now();
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glBindVertexArray(m_emptyVao);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // 1) Active source -> environment cubemap.
    GLShader* sourceShader = nullptr;
    if (environment.Source == EnvironmentSource::EquirectangularHdr)
    {
        sourceShader = m_equirectShader.get();
        sourceShader->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, GetOrCreatePanorama(environment.Hdri));
        sourceShader->SetInt("uEquirectangularMap", 0);
        sourceShader->SetFloat("uRotation", glm::radians(environment.RotationDegrees));
        sourceShader->SetFloat("uIntensity", std::exp2(environment.ExposureEV));
    }
    else
    {
        sourceShader = m_skyGenShader.get();
        sourceShader->Use();
        sourceShader->SetVec3("uSunDirection", glm::normalize(sun.Direction));
        const float elevation = glm::degrees(std::asin(glm::clamp(-glm::normalize(sun.Direction).y, -1.0f, 1.0f)));
        const DayNightState dayNight = sky.EnableDayNightCycle ? EvaluateDayNight(elevation) : DayNightState{};
        sourceShader->SetVec3("uSunColor", sun.Color * dayNight.SunTint);
        sourceShader->SetFloat("uSunIntensity", sky.SunIntensity);
        sourceShader->SetFloat("uSunAngularRadius", glm::radians(sky.SunAngularRadiusDeg));
        sourceShader->SetVec3("uZenithColor", sky.ZenithColor);
        sourceShader->SetVec3("uHorizonColor", sky.HorizonColor);
        sourceShader->SetVec3("uGroundColor", sky.GroundColor);
        sourceShader->SetVec3("uNightZenithColor", sky.NightZenithColor);
        sourceShader->SetVec3("uNightHorizonColor", sky.NightHorizonColor);
        sourceShader->SetFloat("uNightSkyIntensity", sky.NightSkyIntensity);
        sourceShader->SetFloat("uNightHorizonGlow", sky.NightHorizonGlow);
        sourceShader->SetFloat("uSkyIntensity", sky.SkyIntensity);
        sourceShader->SetBool("uEnableDayNightCycle", sky.EnableDayNightCycle);
    }

    glViewport(0, 0, kEnvSize, kEnvSize);
    for (int face = 0; face < 6; ++face)
    {
        sourceShader->SetMat3("uFaceBasis", FaceBasisMatrix(face));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_envCubemap, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Mips of the env map feed both the irradiance pre-blur and the
    // prefilter's PDF-matched sampling.
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // 2) Irradiance convolution
    m_irradianceShader->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCubemap);
    m_irradianceShader->SetInt("uEnvMap", 0);
    m_irradianceShader->SetFloat("uSampleDelta", fastUpdate ? 0.10f : 0.05f);

    glViewport(0, 0, kIrradianceSize, kIrradianceSize);
    for (int face = 0; face < 6; ++face)
    {
        m_irradianceShader->SetMat3("uFaceBasis", FaceBasisMatrix(face));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_irradianceCubemap, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // 3) GGX prefilter mip chain
    m_prefilterShader->Use();
    m_prefilterShader->SetInt("uEnvMap", 0);
    m_prefilterShader->SetFloat("uEnvResolution", static_cast<float>(kEnvSize));
    // Initial/HDRI bakes retain the full offline-quality integration. Only a
    // moving procedural sun uses the cheaper update, turning the periodic
    // render-thread spike into a small bounded refresh.
    m_prefilterShader->SetInt("uSampleCount", fastUpdate ? 128 : 1024);

    for (int mip = 0; mip < kPrefilterMips; ++mip)
    {
        const int mipSize = kPrefilterSize >> mip;
        const float roughness = static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1);

        m_prefilterShader->SetFloat("uRoughness", roughness);
        glViewport(0, 0, mipSize, mipSize);
        for (int face = 0; face < 6; ++face)
        {
            m_prefilterShader->SetMat3("uFaceBasis", FaceBasisMatrix(face));
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, m_prefilterCubemap, mip);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glFinish(); // Bake timing is a rare diagnostic event, never a per-frame sync.
    const float milliseconds = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - bakeStart).count();
    const std::string sourceName = environment.Source == EnvironmentSource::EquirectangularHdr ? "HDRI" : "procedural sky";
    log::Info("IBL: " + sourceName + " re-baked in " + std::to_string(milliseconds) + " ms");
}

void GLEnvironment::BindEnvironmentMap(int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCubemap);
}

void GLEnvironment::BindIrradianceMap(int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_irradianceCubemap);
}

void GLEnvironment::BindPrefilterMap(int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_prefilterCubemap);
}

void GLEnvironment::BindBrdfLut(int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_brdfLut);
}

} // namespace engine
