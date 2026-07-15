#pragma once

#include <chrono>
#include <memory>
#include <unordered_map>

#include <glm/glm.hpp>

#include "engine/backend/gl/GLShader.h"
#include "engine/scene/Scene.h"

namespace engine {

// Bakes and owns the image-based lighting set derived from a procedural sky
// or a linear HDR equirectangular panorama: environment cubemap, irradiance
// convolution for diffuse ambient, GGX-prefiltered specular mip chain, and
// the split-sum BRDF LUT (the same lookup VRF samples in EnvBRDF /
// environment.slang). A bake key prevents unchanged inputs from doing GPU
// work on subsequent frames.
class GLEnvironment
{
public:
    GLEnvironment(const std::string& shaderDir);
    ~GLEnvironment();

    GLEnvironment(const GLEnvironment&) = delete;
    GLEnvironment& operator=(const GLEnvironment&) = delete;

    // Re-bakes only when the active source or an IBL-affecting input changes.
    void EnsureBaked(const DirectionalLight& sun, const SkySettings& sky, const EnvironmentSettings& environment);

    void BindEnvironmentMap(int unit) const;   // skybox + raw reflections
    void BindIrradianceMap(int unit) const;    // diffuse ambient
    void BindPrefilterMap(int unit) const;     // roughness-indexed specular
    void BindBrdfLut(int unit) const;

    unsigned int EnvironmentMapId() const { return m_envCubemap; }
    unsigned int IrradianceMapId() const { return m_irradianceCubemap; }
    unsigned int PrefilterMapId() const { return m_prefilterCubemap; }
    unsigned int BrdfLutId() const { return m_brdfLut; }

    static constexpr int kPrefilterMips = 5;

private:
    void Bake(const DirectionalLight& sun, const SkySettings& sky,
              const EnvironmentSettings& environment, bool fastUpdate);
    unsigned int GetOrCreatePanorama(const std::shared_ptr<HdrImageData>& image);

    unsigned int m_envCubemap = 0;        // 256^2, mipmapped for prefilter source
    unsigned int m_irradianceCubemap = 0; // 32^2
    unsigned int m_prefilterCubemap = 0;  // 128^2, kPrefilterMips levels
    unsigned int m_brdfLut = 0;           // 512^2 RG16F
    unsigned int m_fbo = 0;
    unsigned int m_emptyVao = 0;

    std::unique_ptr<GLShader> m_skyGenShader;
    std::unique_ptr<GLShader> m_equirectShader;
    std::unique_ptr<GLShader> m_irradianceShader;
    std::unique_ptr<GLShader> m_prefilterShader;
    std::unique_ptr<GLShader> m_brdfShader;
    std::unordered_map<std::shared_ptr<HdrImageData>, unsigned int> m_panoramaCache;

    struct BakeKey
    {
        glm::vec3 SunDir{0.0f};
        glm::vec3 SunColor{0.0f};
        float SunIntensity = -1.0f;
        SkySettings Sky;
        EnvironmentSource Source = EnvironmentSource::ProceduralSky;
        const HdrImageData* Hdri = nullptr;
        float ExposureEV = 0.0f;
        float RotationDegrees = 0.0f;
        bool Valid = false;
    } m_lastBake;
    std::chrono::steady_clock::time_point m_lastBakeTime{};
};

} // namespace engine
