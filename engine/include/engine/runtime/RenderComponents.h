#pragma once

#include "engine/asset/AssetGuid.h"
#include "engine/core/Camera.h"
#include "engine/runtime/ComponentRegistry.h"
#include "engine/scene/Environment.h"
#include "engine/scene/Material.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Texture.h"

#include <memory>
#include <string_view>

namespace engine::runtime
{

inline constexpr std::string_view kMeshRendererComponent = "Engine.MeshRenderer";
inline constexpr std::string_view kPointLightComponent = "Engine.PointLight";
inline constexpr std::string_view kSpotLightComponent = "Engine.SpotLight";
inline constexpr std::string_view kAreaLightComponent = "Engine.AreaLight";
inline constexpr std::string_view kDirectionalLightComponent = "Engine.DirectionalLight";
inline constexpr std::string_view kCameraComponent = "Engine.Camera";
inline constexpr std::string_view kEnvironmentComponent = "Engine.Environment";

struct MeshRendererComponent
{
    assets::AssetGuid MeshAsset;
    assets::AssetGuid MaterialAsset;
    std::shared_ptr<MeshData> Mesh;
    Material Mat;
    bool CastsShadows = true;
    bool AlwaysVisible = false;
    bool AllowInstancing = true;
    bool Static = true;
    bool AllowBatching = true;
    uint64_t BatchGroupId = 0;
    float MaxDrawDistance = 0.0f;
    // Runtime resolver cache keys; intentionally not reflected/serialized.
    assets::AssetGuid ResolvedMeshAsset;
    assets::AssetGuid ResolvedMaterialAsset;
};

struct PointLightComponent
{
    glm::vec3 Color{1.0f};
    float Intensity = 20.0f;
    float Radius = 15.0f;
    bool CastsShadows = false;
    assets::AssetGuid CookieAsset;
    std::shared_ptr<TextureData> Cookie;
    assets::AssetGuid ResolvedCookieAsset;
};

struct SpotLightComponent
{
    glm::vec3 Color{1.0f};
    float Intensity = 30.0f;
    float Range = 25.0f;
    float InnerConeDegrees = 20.0f;
    float OuterConeDegrees = 35.0f;
    bool CastsShadows = false;
    assets::AssetGuid CookieAsset;
    std::shared_ptr<TextureData> Cookie;
    assets::AssetGuid ResolvedCookieAsset;
};

struct AreaLightComponent
{
    glm::vec3 Color{1.0f};
    float Intensity = 100.0f;
    float Range = 30.0f;
    glm::vec2 Size{2.0f, 1.0f};
    glm::vec2 Softness{0.15f};
    float BarnAngleDegrees = 55.0f;
    float MinimumRoughness = 0.08f;
    bool CastsShadows = false;
    assets::AssetGuid CookieAsset;
    std::shared_ptr<TextureData> Cookie;
    assets::AssetGuid ResolvedCookieAsset;
};

struct DirectionalLightComponent
{
    glm::vec3 Color{1.0f, 0.96f, 0.88f};
    float Intensity = 3.0f;
    bool CastsShadows = true;
};

struct CameraComponent
{
    CameraProjection Projection = CameraProjection::Perspective;
    float FieldOfViewDegrees = 60.0f;
    float OrthographicSize = 10.0f;
    float NearPlane = 0.05f;
    float FarPlane = 500.0f;
    bool Primary = false;
};

struct EnvironmentComponent
{
    EnvironmentSource Source = EnvironmentSource::ProceduralSky;
    assets::AssetGuid HdriAsset;
    std::shared_ptr<HdrImageData> Hdri;
    assets::AssetGuid ResolvedHdriAsset;
    float RotationDegrees = 0.0f;
    float ExposureEV = 0.0f;
    float BackgroundExposureEV = 0.0f;
    bool VisibleBackground = true;
    float SkyIntensity = 0.45f;
    bool DayNightCycle = true;
    float StarDensity = 0.0015f;
    float StarIntensity = 1.0f;
    float MilkyWayIntensity = 0.35f;
};

bool RegisterBuiltinRenderComponents(ComponentRegistry& registry,
                                     std::string* error = nullptr);

} // namespace engine::runtime
