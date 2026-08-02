#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "engine/scene/ColorGrading.h"

namespace engine {

enum class AntiAliasingMode
{
    None,
    Fxaa,
    Taa
};

struct SkySettings
{
    // The environment may continue to light reflections while its visual
    // background is hidden (editor solid-color/reference viewports).
    bool VisibleBackground = true;
    glm::vec3 ZenithColor{0.18f, 0.32f, 0.66f};
    glm::vec3 HorizonColor{0.72f, 0.80f, 0.94f};
    glm::vec3 GroundColor{0.23f, 0.21f, 0.19f};
    glm::vec3 NightZenithColor{0.003f, 0.008f, 0.028f};
    glm::vec3 NightHorizonColor{0.018f, 0.028f, 0.070f};
    glm::vec3 MilkyWayColor{0.22f, 0.30f, 0.65f};
    glm::vec3 StarWarmColor{1.0f, 0.68f, 0.46f};
    glm::vec3 StarCoolColor{0.62f, 0.78f, 1.0f};
    float SunAngularRadiusDeg = 1.2f;
    float SunIntensity = 80.0f;
    float SkyIntensity = 1.0f;
    float NightSkyIntensity = 1.0f;
    float NightHorizonGlow = 1.0f;
    float StarIntensity = 4.0f;
    float StarDensity = 0.006f;
    float StarSize = 1.0f;
    float StarTwinkle = 0.18f;
    float StarTwinkleSpeed = 1.0f;
    float MilkyWayIntensity = 0.32f;
    float NightSkyRotationDegrees = 0.0f;
    float NightSkyRotationSpeed = 0.35f;
    bool StarsEnabled = true;
    bool MilkyWayEnabled = true;
    bool AnimateNightSky = true;
    bool EnableDayNightCycle = true;
};

struct PostProcessSettings
{
    bool Enabled = true;
    float Exposure = 2.2f;
    float BloomStrength = 0.12f;
    float BloomThreshold = 1.0f;
    float ShoulderStrength = 0.15f;
    float LinearStrength = 0.50f;
    float LinearAngle = 0.10f;
    float ToeStrength = 0.20f;
    float ToeNumerator = 0.02f;
    float ToeDenominator = 0.30f;
    float WhitePoint = 8.0f;

    bool AutoExposure = false;
    float AutoExposureKey = 0.18f;
    float AutoExposureMin = 0.4f;
    float AutoExposureMax = 3.0f;
    float AutoExposureSpeed = 1.8f;

    float Saturation = 1.0f;
    float Contrast = 1.0f;
    glm::vec3 ColorTint{1.0f, 1.0f, 1.0f};

    std::shared_ptr<ColorGradingLutData> ColorLut;
    float ColorLutWeight = 0.0f;

    AntiAliasingMode AntiAliasing = AntiAliasingMode::Taa;
    float FxaaSubpixel = 0.75f;
    float FxaaEdgeThreshold = 0.125f;
    float FxaaEdgeThresholdMin = 0.0312f;
    // Conservative defaults: the Halton sequence remains effective without
    // introducing visible sub-pixel motion in static editor or sandbox views.
    float TaaHistoryWeight = 0.95f;
    float TaaSharpen = 0.06f;
    float TaaJitterScale = 0.5f;
    float TaaDepthThreshold = 0.0025f;
    bool LogPerformance = false;
};

struct FogSettings
{
    bool Enabled = false;
    glm::vec3 Color{0.35f, 0.40f, 0.50f};
    float Opacity = 0.85f;
    float Start = 20.0f;
    float End = 120.0f;
    float DistanceExponent = 1.6f;
    float HeightFadeTop = 25.0f;
    float HeightFadeBottom = 0.0f;
    float HeightExponent = 1.0f;
};

struct ShadowSettings
{
    float MaxDistance = 150.0f;
    float CascadeSplitLambda = 0.65f;
    float CascadeBlendFraction = 0.10f;
    bool DebugCascades = false;
    bool LogPerformance = false;
};

struct VisibilitySettings
{
    bool Enabled = true;
    bool FrustumCulling = true;
    bool DistanceCulling = true;
    // Zero uses the active camera far plane. Individual instances may choose
    // a shorter distance through MeshInstance::MaxDrawDistance.
    float MaxDistance = 0.0f;
    bool DebugBounds = false;
    bool DebugCulledBounds = true;

    // GPU Hi-Z results are consumed asynchronously by the CPU submission path.
    // Motion invalidates history conservatively until a fresh pyramid exists.
    bool GpuOcclusionCulling = true;
    uint32_t OcclusionConfirmationFrames = 2;
    uint32_t OcclusionMaxHiddenFrames = 30;
    // Bias is expressed in normalized device depth. Perspective depth is
    // highly non-linear, so this intentionally stays much smaller than a
    // typical shadow-map bias.
    float OcclusionDepthBias = 0.0001f;
    float OcclusionBoundsInflation = 0.08f;
    float OcclusionCameraPositionThreshold = 0.02f;
    float OcclusionCameraRotationThresholdDeg = 0.25f;
    bool DebugOcclusion = false;
};

struct LodSettings
{
    bool Enabled = true;
    // Largest allowed projected simplification error.  One pixel preserves
    // the intended silhouette while allowing distant geometry to become much
    // cheaper.
    float TargetScreenSpaceErrorPixels = 1.0f;
    // A band around the target error prevents rapid level changes when the
    // camera hovers around a transition.
    float HysteresisFraction = 0.15f;
    // Positive values favor detail; negative values favor cheaper geometry.
    float Bias = 0.0f;
    // UINT32_MAX means automatic selection. This is useful for visual QA of
    // an individual level without backend-specific debug switches.
    uint32_t ForcedLevel = UINT32_MAX;
};

struct InstancingSettings
{
    // Compatible meshes and materials are submitted with one native
    // instanced draw. Setting this to false preserves the same instance-data
    // path but emits one draw per object for diagnostics and comparison.
    bool Enabled = true;
    uint32_t MinimumBatchSize = 2;

    // HISM builds a spatial hierarchy per compatible instance group before
    // leaf-level visibility tests. Small groups stay on the direct path.
    bool HierarchicalCulling = true;
    uint32_t HismMinimumGroupSize = 32;
    uint32_t HismLeafSize = 8;
};

struct GeometryBatchingSettings
{
    bool Enabled = true;
    bool StaticBatching = true;
    bool DynamicBatching = true;
    bool PreferInstancingForRepeatedMeshes = true;
    uint32_t MinimumStaticBatchSize = 2;
    uint32_t MinimumDynamicBatchSize = 3;
    // Dynamic batching is deliberately limited to small source meshes. Large
    // meshes keep their individual culling and GPU-instancing path.
    uint32_t DynamicMaxSourceVertices = 512;
    uint32_t MaximumVerticesPerBatch = 65'536;
    uint32_t MaximumIndicesPerBatch = 196'608;
    uint32_t MaximumCachedBatches = 2'048;
    // Spatial cells retain useful frustum/Hi-Z granularity after geometry is
    // combined. Non-positive values disable spatial partitioning.
    float SpatialCellSize = 32.0f;
    // With TAA active, movable objects must remain unchanged for this many
    // frames before they are safely baked into a dynamic batch.
    uint32_t DynamicStabilityFrames = 2;
    bool PreserveMotionVectors = true;
};

} // namespace engine
