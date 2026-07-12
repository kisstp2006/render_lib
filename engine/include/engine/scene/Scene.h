#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/scene/Mesh.h"
#include "engine/scene/Texture.h"

namespace engine {

// PBR parameters in the metallic/roughness convention, matching the
// Source 2 vr_standard/complex material model closely enough for this engine's
// purposes (see pbr.slang: SpecularColor/AlbedoColor/Roughness terms).
// Scalars act as factors; texture slots are optional and multiply into them.
// MRAO packing follows the Source 2 convention: R=metalness, G=roughness, B=AO.
struct Material
{
    glm::vec3 Albedo{0.8f, 0.8f, 0.8f};
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    glm::vec3 Emissive{0.0f};
    float AmbientOcclusion = 1.0f;
    // Source materials default specular to a flat 0.04 (4%) dielectric F0,
    // exposed here in case a material wants a non-standard value (e.g. skin, wax).
    float SpecularF0 = 0.04f;

    std::shared_ptr<TextureData> AlbedoMap;
    std::shared_ptr<TextureData> NormalMap;
    std::shared_ptr<TextureData> MraoMap;
};

// Procedural sky feeding both the visible skybox and the IBL bake. HDR:
// SunIntensity is deliberately way above 1.0 so the sun disk drives bloom
// and specular highlights like a Source 2 sun does.
struct SkySettings
{
    glm::vec3 ZenithColor{0.18f, 0.32f, 0.66f};
    glm::vec3 HorizonColor{0.72f, 0.80f, 0.94f};
    glm::vec3 GroundColor{0.23f, 0.21f, 0.19f};
    float SunAngularRadiusDeg = 1.2f;
    float SunIntensity = 80.0f;
    float SkyIntensity = 1.0f;
};

// Mirrors the tonemapper parameterization in VRF's post_processing.frag.slang
// (Uncharted curve with Source's shoulder/linear/toe split + white point).
struct PostProcessSettings
{
    bool Enabled = true;
    // The classic Uncharted curve wants a healthy exposure bias to hit a
    // pleasing mid-gray (VRF multiplies ToneMapScalar * ExposureBias too).
    float Exposure = 2.2f;
    float BloomStrength = 0.12f;        // ADD-bloom path weight
    float BloomThreshold = 1.0f;        // luminance where bloom starts to pick up
    float ShoulderStrength = 0.15f;
    float LinearStrength = 0.50f;
    float LinearAngle = 0.10f;
    float ToeStrength = 0.20f;
    float ToeNumerator = 0.02f;
    float ToeDenominator = 0.30f;
    float WhitePoint = 8.0f;

    // Source 2-style tonemap controller: adapts exposure toward
    // Key / averageLuminance, clamped to [AutoExposureMin, AutoExposureMax]
    // as multipliers on Exposure.
    bool AutoExposure = false;
    float AutoExposureKey = 0.18f;
    float AutoExposureMin = 0.4f;
    float AutoExposureMax = 3.0f;
    float AutoExposureSpeed = 1.8f; // 1/seconds, higher adapts faster

    // LDR grade applied after tonemap (stand-in for Source's color
    // correction LUTs until LUT loading lands).
    float Saturation = 1.0f;
    float Contrast = 1.0f;
    glm::vec3 ColorTint{1.0f, 1.0f, 1.0f};
};

struct DirectionalLight
{
    glm::vec3 Direction{-0.4f, -0.85f, -0.35f};
    glm::vec3 Color{1.0f, 0.96f, 0.88f};
    float Intensity = 3.0f;
    bool CastsShadows = true;
};

struct PointLight
{
    glm::vec3 Position{0.0f};
    glm::vec3 Color{1.0f};
    float Intensity = 20.0f;
    float Radius = 15.0f;
};

// Source 2-style spot (see VRF SceneLight: EntityType.Spot with
// inner/outer angles). The first enabled spot with CastsShadows gets a
// perspective shadow map.
struct SpotLight
{
    glm::vec3 Position{0.0f};
    glm::vec3 Direction{0.0f, -1.0f, 0.0f};
    glm::vec3 Color{1.0f};
    float Intensity = 30.0f;
    float Range = 25.0f;
    float InnerConeDeg = 20.0f;
    float OuterConeDeg = 35.0f;
    bool CastsShadows = false;
    bool Enabled = true;
};

// Distance x height gradient fog, the same shape as VRF's ApplyGradientFog
// (fog.slang): both components are saturated ramps raised to an exponent,
// multiplied together and against Opacity, then mixed toward Color.
struct FogSettings
{
    bool Enabled = false;
    glm::vec3 Color{0.35f, 0.40f, 0.50f}; // linear HDR, pre-tonemap
    float Opacity = 0.85f;
    float Start = 20.0f;           // view distance where fog begins
    float End = 120.0f;            // fully fogged distance
    float DistanceExponent = 1.6f;
    float HeightFadeTop = 25.0f;   // no fog contribution above this height
    float HeightFadeBottom = 0.0f; // full height factor below this height
    float HeightExponent = 1.0f;
};

struct MeshInstance
{
    std::shared_ptr<MeshData> Mesh;
    Material Mat;
    glm::mat4 Transform{1.0f};
};

// A minimal scene container: no ECS, just flat lists. Enough for the sandbox
// and a reasonable starting point before a real scene graph is needed.
class Scene
{
public:
    std::vector<MeshInstance>& Instances() { return m_instances; }
    const std::vector<MeshInstance>& Instances() const { return m_instances; }

    std::vector<PointLight>& PointLights() { return m_pointLights; }
    const std::vector<PointLight>& PointLights() const { return m_pointLights; }

    std::vector<SpotLight>& SpotLights() { return m_spotLights; }
    const std::vector<SpotLight>& SpotLights() const { return m_spotLights; }

    DirectionalLight Sun;
    SkySettings Sky;
    PostProcessSettings PostProcess;
    FogSettings Fog;

    void AddInstance(std::shared_ptr<MeshData> mesh, const Material& mat, const glm::mat4& transform);
    void AddPointLight(const PointLight& light);
    void AddSpotLight(const SpotLight& light);

private:
    std::vector<MeshInstance> m_instances;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;
};

} // namespace engine
