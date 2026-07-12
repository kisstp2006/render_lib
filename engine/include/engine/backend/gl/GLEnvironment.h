#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "engine/backend/gl/GLShader.h"
#include "engine/scene/Scene.h"

namespace engine {

// Bakes and owns the image-based lighting set derived from the procedural
// sky: environment cubemap (also used as the visible skybox), irradiance
// convolution for diffuse ambient, GGX-prefiltered specular mip chain, and
// the split-sum BRDF LUT (the same lookup VRF samples in EnvBRDF /
// environment.slang). Bake() is cheap enough to re-run whenever the sun or
// sky settings change.
class GLEnvironment
{
public:
    GLEnvironment(const std::string& shaderDir);
    ~GLEnvironment();

    GLEnvironment(const GLEnvironment&) = delete;
    GLEnvironment& operator=(const GLEnvironment&) = delete;

    // Re-bakes if the sun/sky inputs changed since the last call.
    void EnsureBaked(const DirectionalLight& sun, const SkySettings& sky);

    void BindEnvironmentMap(int unit) const;   // skybox + raw reflections
    void BindIrradianceMap(int unit) const;    // diffuse ambient
    void BindPrefilterMap(int unit) const;     // roughness-indexed specular
    void BindBrdfLut(int unit) const;

    static constexpr int kPrefilterMips = 5;

private:
    void Bake(const DirectionalLight& sun, const SkySettings& sky);
    void RenderToCubemapFace(unsigned int cubemap, int face, int mip, int size);

    unsigned int m_envCubemap = 0;        // 256^2, mipmapped for prefilter source
    unsigned int m_irradianceCubemap = 0; // 32^2
    unsigned int m_prefilterCubemap = 0;  // 128^2, kPrefilterMips levels
    unsigned int m_brdfLut = 0;           // 512^2 RG16F
    unsigned int m_fbo = 0;
    unsigned int m_emptyVao = 0;

    std::unique_ptr<GLShader> m_skyGenShader;
    std::unique_ptr<GLShader> m_irradianceShader;
    std::unique_ptr<GLShader> m_prefilterShader;
    std::unique_ptr<GLShader> m_brdfShader;

    struct BakeKey
    {
        glm::vec3 SunDir{0.0f};
        glm::vec3 SunColor{0.0f};
        float SunIntensity = -1.0f;
        SkySettings Sky;
        bool Valid = false;
    } m_lastBake;
};

} // namespace engine
