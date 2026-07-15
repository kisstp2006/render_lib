#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "engine/scene/Material.h"

namespace engine::vulkan {

// Stable descriptor contract shared conceptually with shaders/vk. Keeping
// these indices named prevents pipeline code from scattering magic numbers.
namespace binding {
constexpr uint32_t FrameUniforms = 0;
constexpr uint32_t IrradianceMap = 1;
constexpr uint32_t PrefilteredMap = 2;
constexpr uint32_t BrdfLut = 3;
constexpr uint32_t SunShadowMap = 4;
constexpr uint32_t EnvironmentMap = 8;
constexpr uint32_t PointShadowMaps = 9;
constexpr uint32_t LocalShadowAtlas = 10;
constexpr uint32_t LightCookieAtlas = 11;
constexpr uint32_t InstanceTransforms = 0;

constexpr uint32_t MaterialUniforms = 0;
constexpr uint32_t BaseColorMap = 1;
constexpr uint32_t NormalMap = 2;
constexpr uint32_t MetallicRoughnessMap = 3;
constexpr uint32_t OcclusionMap = 4;
constexpr uint32_t EmissiveMap = 5;
} // namespace binding

enum MaterialTextureFlag : uint32_t
{
    HasBaseColorMap = 1u << 0u,
    HasNormalMap = 1u << 1u,
    HasMetallicRoughnessMap = 1u << 2u,
    HasOcclusionMap = 1u << 3u,
    HasEmissiveMap = 1u << 4u,
    UsesAlphaMask = 1u << 5u,
    HasLegacyMraoMap = 1u << 6u,
};

// std140-compatible data blocks for the first Vulkan PBR pipeline. All vec3
// values are represented as vec4 so layout is identical across compilers.
struct alignas(16) FrameUniforms
{
    glm::mat4 View{1.0f};
    glm::mat4 Projection{1.0f};
    glm::mat4 PreviousViewProjection{1.0f};
    glm::vec4 CameraPosition{0.0f};
    glm::vec4 SunDirectionIntensity{0.0f, -1.0f, 0.0f, 1.0f};
    glm::vec4 SunColor{1.0f};
    glm::vec4 SkyZenithIntensity{0.18f, 0.32f, 0.66f, 1.0f};
    glm::vec4 SkyHorizonPostEnabled{0.72f, 0.80f, 0.94f, 1.0f};
    glm::vec4 SkyGroundExposure{0.23f, 0.21f, 0.19f, 1.0f};
    glm::vec4 SkySunParameters{80.0f, 0.02f, 1.0f, 1.0f};
    glm::vec4 PostCurve0{0.15f, 0.50f, 0.10f, 0.20f};
    glm::vec4 PostCurve1{0.02f, 0.30f, 1.0f, 1.0f};
    glm::vec4 PostGrade{1.0f}; // x=contrast, yzw=tint
    glm::vec4 FogColorOpacity{0.35f, 0.40f, 0.50f, 0.0f};
    glm::vec4 FogStartEndExponents{20.0f, 120.0f, 1.6f, 1.0f};
    glm::vec4 FogHeightEnabled{25.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 NightZenithIntensity{0.003f, 0.008f, 0.028f, 1.0f};
    glm::vec4 NightHorizonGlow{0.018f, 0.028f, 0.070f, 1.0f};
    glm::vec4 MilkyWayColorIntensity{0.22f, 0.30f, 0.65f, 0.32f};
    glm::vec4 StarWarmDensity{1.0f, 0.68f, 0.46f, 0.006f};
    glm::vec4 StarCoolSize{0.62f, 0.78f, 1.0f, 1.0f};
    glm::vec4 StarAnimation{4.0f, 0.18f, 1.0f, 0.0f};
    glm::vec4 NightRotationFlags{0.0f, 0.35f, 0.0f, 0.0f};
    glm::vec4 SkyFeatureFlags{1.0f, 1.0f, 1.0f, 1.0f};
    glm::mat4 CascadeMatrices[4]{};
    glm::vec4 CascadeSplits{25.0f, 50.0f, 100.0f, 150.0f};
    glm::vec4 ShadowParameters{0.1f, 1.0f, 0.0f, 0.0f};
    glm::uvec4 LightCounts{0u};
    glm::vec4 PointPositionRadius[8]{};
    glm::vec4 PointColor[8]{};
    glm::vec4 SpotPositionRange[4]{};
    glm::vec4 SpotDirectionCosOuter[4]{};
    glm::vec4 SpotColorCosInner[4]{};
    glm::vec4 AreaPositionRange[4]{};
    glm::vec4 AreaDirectionMinRoughness[4]{};
    glm::vec4 AreaRightHalfWidth[4]{};
    glm::vec4 AreaUpHalfHeight[4]{};
    glm::vec4 AreaColor[4]{};
    glm::vec4 AreaSoftness[4]{};
    glm::vec4 PointShadowCookie[8]{};
    glm::mat4 SpotMatrices[4]{};
    glm::vec4 SpotShadowRects[4]{};
    glm::vec4 SpotCookieData[4]{};
    glm::mat4 AreaMatrices[4]{};
    glm::vec4 AreaShadowRects[4]{};
    glm::vec4 AreaCookieData[4]{};
};

struct alignas(16) ShadowUniforms
{
    glm::mat4 LightViewProjection{1.0f};
    glm::vec4 LightPositionRange{0.0f};
};

struct alignas(16) MaterialUniforms
{
    glm::vec4 BaseColorFactor{1.0f};
    glm::vec4 EmissiveMetallic{0.0f};
    glm::vec4 RoughnessAoAlphaCutoff{0.5f, 1.0f, 0.5f, 0.0f};
    glm::uvec4 TextureFlags{0u};
};

struct alignas(16) BoundsDebugConstants
{
    glm::vec4 Minimum{0.0f};
    glm::vec4 Maximum{0.0f};
    glm::vec4 Color{0.1f, 3.0f, 0.25f, 1.0f};
};

struct alignas(16) EnvironmentBakeConstants
{
    glm::vec4 SunDirectionIntensity{0.0f, -1.0f, 0.0f, 80.0f};
    glm::vec4 SunColorAngularRadius{1.0f, 1.0f, 1.0f, 0.02f};
    glm::vec4 ZenithIntensity{0.18f, 0.32f, 0.66f, 1.0f};
    glm::vec4 HorizonCycle{0.72f, 0.80f, 0.94f, 1.0f};
    glm::vec4 GroundExposure{0.23f, 0.21f, 0.19f, 1.0f};
    glm::vec4 NightZenithIntensity{0.003f, 0.008f, 0.028f, 1.0f};
    glm::vec4 NightHorizonGlow{0.018f, 0.028f, 0.070f, 1.0f};
    glm::vec4 BakeParameters{0.0f, 256.0f, 0.0f, 0.0f};
};

struct alignas(16) PostUniforms
{
    // x=exposure, y=bloom strength, z=post enabled, w=LUT weight
    glm::vec4 ExposureBloomPostLut{1.0f, 0.0f, 1.0f, 0.0f};
    glm::vec4 Curve0{0.15f, 0.50f, 0.10f, 0.20f};
    // x=toe numerator, y=toe denominator, z=white scale, w=saturation
    glm::vec4 Curve1{0.02f, 0.30f, 1.0f, 1.0f};
    // x=contrast, yzw=color tint
    glm::vec4 Grade{1.0f};
    glm::vec4 LutDomainMinSize{0.0f, 0.0f, 0.0f, 2.0f};
    // xyz=LUT max, w=dither enabled
    glm::vec4 LutDomainMaxDither{1.0f};
    // xyz=FXAA subpixel/edge/edge-min, w=reserved
    glm::vec4 Fxaa{0.75f, 0.125f, 0.0312f, 0.0f};
};

struct alignas(16) BloomConstants
{
    glm::vec4 Parameters{0.0f}; // x=first pass, y=threshold, z=exposure
};

struct alignas(16) TaaConstants
{
    glm::vec4 Parameters{0.0f};
    glm::vec4 Depth{0.1f, 1000.0f, 0.0f, 0.0f};
};

static_assert(sizeof(FrameUniforms) % 16 == 0);
static_assert(sizeof(ShadowUniforms) == 80);
static_assert(sizeof(MaterialUniforms) == 64);
static_assert(sizeof(BoundsDebugConstants) == 48);
static_assert(sizeof(EnvironmentBakeConstants) == 128);
static_assert(sizeof(PostUniforms) == 112);
static_assert(sizeof(BloomConstants) == 16);
static_assert(sizeof(TaaConstants) == 32);

inline MaterialUniforms PackMaterialUniforms(const Material& material)
{
    MaterialUniforms uniforms;
    uniforms.BaseColorFactor = glm::vec4(material.Albedo, material.BaseColorAlpha);
    uniforms.EmissiveMetallic = glm::vec4(material.Emissive, material.Metallic);
    uniforms.RoughnessAoAlphaCutoff = {
        material.Roughness, material.AmbientOcclusion, material.AlphaCutoff, material.SpecularF0};

    uint32_t flags = 0;
    if (material.AlbedoMap) flags |= HasBaseColorMap;
    if (material.NormalMap) flags |= HasNormalMap;
    if (material.MetallicRoughnessMap) flags |= HasMetallicRoughnessMap;
    if (material.MraoMap) flags |= HasLegacyMraoMap;
    if (material.OcclusionMap) flags |= HasOcclusionMap;
    if (material.EmissiveMap) flags |= HasEmissiveMap;
    if (material.Alpha == Material::AlphaMode::Mask) flags |= UsesAlphaMask;
    uniforms.TextureFlags.x = flags;
    return uniforms;
}

} // namespace engine::vulkan
