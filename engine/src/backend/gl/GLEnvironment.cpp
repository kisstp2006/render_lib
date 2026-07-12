#include "engine/backend/gl/GLEnvironment.h"
#include "engine/core/Log.h"

#include <glad/gl.h>

#include <cstring>

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
    m_skyGenShader = std::make_unique<GLShader>(shaderDir + "/gl/fullscreen.vert", shaderDir + "/gl/sky_gen.frag");
    m_irradianceShader = std::make_unique<GLShader>(shaderDir + "/gl/fullscreen.vert", shaderDir + "/gl/irradiance.frag");
    m_prefilterShader = std::make_unique<GLShader>(shaderDir + "/gl/fullscreen.vert", shaderDir + "/gl/prefilter.frag");
    m_brdfShader = std::make_unique<GLShader>(shaderDir + "/gl/fullscreen.vert", shaderDir + "/gl/brdf_lut.frag");

    glGenFramebuffers(1, &m_fbo);
    glGenVertexArrays(1, &m_emptyVao);

    m_envCubemap = CreateCubemap(kEnvSize, true);
    m_irradianceCubemap = CreateCubemap(kIrradianceSize, false);
    m_prefilterCubemap = CreateCubemap(kPrefilterSize, true);

    // BRDF LUT is sun/sky independent: bake it once here.
    glGenTextures(1, &m_brdfLut);
    glBindTexture(GL_TEXTURE_2D, m_brdfLut);
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
    glDeleteTextures(1, &m_envCubemap);
    glDeleteTextures(1, &m_irradianceCubemap);
    glDeleteTextures(1, &m_prefilterCubemap);
    glDeleteTextures(1, &m_brdfLut);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteVertexArrays(1, &m_emptyVao);
}

void GLEnvironment::EnsureBaked(const DirectionalLight& sun, const SkySettings& sky)
{
    const bool dirty = !m_lastBake.Valid
        || m_lastBake.SunDir != sun.Direction
        || m_lastBake.SunColor != sun.Color
        || m_lastBake.SunIntensity != sun.Intensity
        || std::memcmp(&m_lastBake.Sky, &sky, sizeof(SkySettings)) != 0;

    if (!dirty)
        return;

    Bake(sun, sky);

    m_lastBake.SunDir = sun.Direction;
    m_lastBake.SunColor = sun.Color;
    m_lastBake.SunIntensity = sun.Intensity;
    m_lastBake.Sky = sky;
    m_lastBake.Valid = true;
}

void GLEnvironment::Bake(const DirectionalLight& sun, const SkySettings& sky)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glBindVertexArray(m_emptyVao);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // 1) Procedural sky -> environment cubemap
    m_skyGenShader->Use();
    m_skyGenShader->SetVec3("uSunDirection", glm::normalize(sun.Direction));
    m_skyGenShader->SetVec3("uSunColor", sun.Color);
    m_skyGenShader->SetFloat("uSunIntensity", sky.SunIntensity);
    m_skyGenShader->SetFloat("uSunAngularRadius", glm::radians(sky.SunAngularRadiusDeg));
    m_skyGenShader->SetVec3("uZenithColor", sky.ZenithColor);
    m_skyGenShader->SetVec3("uHorizonColor", sky.HorizonColor);
    m_skyGenShader->SetVec3("uGroundColor", sky.GroundColor);
    m_skyGenShader->SetFloat("uSkyIntensity", sky.SkyIntensity);

    glViewport(0, 0, kEnvSize, kEnvSize);
    for (int face = 0; face < 6; ++face)
    {
        m_skyGenShader->SetMat3("uFaceBasis", FaceBasisMatrix(face));
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

    log::Info("IBL: environment re-baked");
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
