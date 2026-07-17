#include "engine/runtime/WorldRenderBridge.h"

#include "engine/core/Camera.h"
#include "engine/scene/Scene.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace engine::runtime
{
namespace
{

glm::vec3 Position(const glm::mat4& transform)
{
    return glm::vec3(transform[3]);
}

glm::vec3 Forward(const glm::mat4& transform)
{
    const glm::vec3 direction = -glm::vec3(transform[2]);
    return glm::length(direction) > 1.0e-6f
        ? glm::normalize(direction) : glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 Up(const glm::mat4& transform)
{
    const glm::vec3 direction = glm::vec3(transform[1]);
    return glm::length(direction) > 1.0e-6f
        ? glm::normalize(direction) : glm::vec3(0.0f, 1.0f, 0.0f);
}

uint64_t TemporalId(assets::AssetGuid guid)
{
    uint64_t value = guid.High ^ (guid.Low + 0x9e3779b97f4a7c15ull +
                                  (guid.High << 6u) + (guid.High >> 2u));
    return value == 0 ? 1 : value;
}

template <typename AssetType, typename Resolver>
void ResolveSharedAsset(assets::AssetGuid requested,
                        assets::AssetGuid& resolved,
                        std::shared_ptr<AssetType>& value,
                        const Resolver& resolver)
{
    if (resolved.IsValid() && requested != resolved)
    {
        value.reset();
        resolved = {};
    }
    if (!requested.IsValid() || (requested == resolved && value) || !resolver)
        return;
    if (std::shared_ptr<AssetType> loaded = resolver(requested))
    {
        value = std::move(loaded);
        resolved = requested;
    }
}

void ApplyCamera(const glm::mat4& world, const CameraComponent& component,
                 Camera& camera)
{
    const glm::vec3 forward = Forward(world);
    camera.Position = Position(world);
    camera.Pitch = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
    camera.Yaw = glm::degrees(std::atan2(forward.z, forward.x));
    camera.Projection = component.Projection;
    camera.FovDegrees = component.FieldOfViewDegrees;
    camera.OrthographicSize = component.OrthographicSize;
    camera.NearPlane = glm::max(component.NearPlane, 0.0001f);
    camera.FarPlane = glm::max(component.FarPlane, camera.NearPlane + 0.001f);
}

} // namespace

WorldRenderSyncResult WorldRenderBridge::Synchronize(World& world, Scene& scene,
                                                     Camera* activeCamera)
{
    WorldRenderSyncResult result;
    scene.Instances().clear();
    scene.PointLights().clear();
    scene.SpotLights().clear();
    scene.AreaLights().clear();
    // A synchronized World owns these render-scene values. Resetting them is
    // important when an editor removes or disables the corresponding
    // component: stale lighting must disappear in the very next frame.
    scene.Sun = DirectionalLight{};
    scene.Environment = EnvironmentSettings{};
    scene.Sky = SkySettings{};

    EntityId fallbackCamera = kInvalidEntity;
    for (const EntityInfo& entity : world.ListEntities())
    {
        if (!entity.ActiveInHierarchy)
            continue;
        const glm::mat4 transform = world.GetWorldTransform(entity.Id);

        if (auto* component = world.IsComponentEnabled(
                entity.Id, std::string(kMeshRendererComponent))
                ? world.GetComponent<MeshRendererComponent>(
                    entity.Id, std::string(kMeshRendererComponent))
                : nullptr)
        {
            ResolveSharedAsset(component->MeshAsset,
                               component->ResolvedMeshAsset,
                               component->Mesh, m_resolvers.Mesh);
            if (component->ResolvedMaterialAsset.IsValid() &&
                component->MaterialAsset != component->ResolvedMaterialAsset)
            {
                component->Mat = Material{};
                component->ResolvedMaterialAsset = {};
            }
            if (component->MaterialAsset.IsValid() &&
                component->MaterialAsset != component->ResolvedMaterialAsset &&
                m_resolvers.Material)
            {
                if (auto material = m_resolvers.Material(component->MaterialAsset))
                {
                    component->Mat = std::move(*material);
                    component->ResolvedMaterialAsset = component->MaterialAsset;
                }
            }
            if (component->Mesh)
            {
                scene.AddInstance(component->Mesh, component->Mat, transform);
                MeshInstance& instance = scene.Instances().back();
                instance.SourceEntity = entity.Id;
                instance.TemporalId = TemporalId(entity.Guid);
                instance.CastsShadows = component->CastsShadows;
                instance.AlwaysVisible = component->AlwaysVisible;
                instance.AllowInstancing = component->AllowInstancing;
                instance.Mobility = component->Static
                    ? MeshMobility::Static : MeshMobility::Movable;
                instance.AllowBatching = component->AllowBatching;
                instance.BatchGroupId = component->BatchGroupId;
                instance.MaxDrawDistance = component->MaxDrawDistance;
                ++result.MeshRenderers;
            }
        }

        if (auto* component = world.IsComponentEnabled(
                entity.Id, std::string(kPointLightComponent))
                ? world.GetComponent<PointLightComponent>(
                    entity.Id, std::string(kPointLightComponent))
                : nullptr)
        {
            ResolveSharedAsset(component->CookieAsset,
                               component->ResolvedCookieAsset,
                               component->Cookie, m_resolvers.Texture);
            PointLight light;
            light.Position = Position(transform);
            light.Color = component->Color;
            light.Intensity = component->Intensity;
            light.Radius = component->Radius;
            light.CastsShadows = component->CastsShadows;
            light.Cookie = component->Cookie;
            scene.AddPointLight(light);
            ++result.PointLights;
        }

        if (auto* component = world.IsComponentEnabled(
                entity.Id, std::string(kSpotLightComponent))
                ? world.GetComponent<SpotLightComponent>(
                    entity.Id, std::string(kSpotLightComponent))
                : nullptr)
        {
            ResolveSharedAsset(component->CookieAsset,
                               component->ResolvedCookieAsset,
                               component->Cookie, m_resolvers.Texture);
            SpotLight light;
            light.Position = Position(transform);
            light.Direction = Forward(transform);
            light.Color = component->Color;
            light.Intensity = component->Intensity;
            light.Range = component->Range;
            light.InnerConeDeg = glm::min(component->InnerConeDegrees,
                                          component->OuterConeDegrees);
            light.OuterConeDeg = component->OuterConeDegrees;
            light.CastsShadows = component->CastsShadows;
            light.Cookie = component->Cookie;
            scene.AddSpotLight(light);
            ++result.SpotLights;
        }

        if (auto* component = world.IsComponentEnabled(
                entity.Id, std::string(kAreaLightComponent))
                ? world.GetComponent<AreaLightComponent>(
                    entity.Id, std::string(kAreaLightComponent))
                : nullptr)
        {
            ResolveSharedAsset(component->CookieAsset,
                               component->ResolvedCookieAsset,
                               component->Cookie, m_resolvers.Texture);
            AreaLight light;
            light.Position = Position(transform);
            light.Direction = Forward(transform);
            light.Up = Up(transform);
            light.Color = component->Color;
            light.Intensity = component->Intensity;
            light.Range = component->Range;
            light.Size = component->Size;
            light.Softness = component->Softness;
            light.BarnAngleDeg = component->BarnAngleDegrees;
            light.MinRoughness = component->MinimumRoughness;
            light.CastsShadows = component->CastsShadows;
            light.Cookie = component->Cookie;
            scene.AddAreaLight(light);
            ++result.AreaLights;
        }

        if (result.DirectionalLight == kInvalidEntity &&
            world.IsComponentEnabled(entity.Id,
                                     std::string(kDirectionalLightComponent)))
        {
            if (const auto* component = world.GetComponent<DirectionalLightComponent>(
                    entity.Id, std::string(kDirectionalLightComponent)))
            {
                scene.Sun.Direction = Forward(transform);
                scene.Sun.Color = component->Color;
                scene.Sun.Intensity = component->Intensity;
                scene.Sun.CastsShadows = component->CastsShadows;
                result.DirectionalLight = entity.Id;
            }
        }

        if (const auto* component = world.IsComponentEnabled(
                entity.Id, std::string(kCameraComponent))
                ? world.GetComponent<CameraComponent>(
                    entity.Id, std::string(kCameraComponent))
                : nullptr)
        {
            if (fallbackCamera == kInvalidEntity)
                fallbackCamera = entity.Id;
            if (activeCamera && (component->Primary || result.ActiveCamera == kInvalidEntity))
            {
                ApplyCamera(transform, *component, *activeCamera);
                result.ActiveCamera = entity.Id;
                if (component->Primary)
                    fallbackCamera = kInvalidEntity;
            }
        }

        if (result.Environment == kInvalidEntity &&
            world.IsComponentEnabled(entity.Id,
                                     std::string(kEnvironmentComponent)))
        {
            if (auto* component = world.GetComponent<EnvironmentComponent>(
                    entity.Id, std::string(kEnvironmentComponent)))
            {
                ResolveSharedAsset(component->HdriAsset,
                                   component->ResolvedHdriAsset,
                                   component->Hdri, m_resolvers.Hdri);
                scene.Environment.Source = component->Source;
                scene.Environment.Hdri = component->Hdri;
                scene.Environment.RotationDegrees = component->RotationDegrees;
                scene.Environment.ExposureEV = component->ExposureEV;
                scene.Environment.BackgroundExposureEV = component->BackgroundExposureEV;
                scene.Sky.VisibleBackground = component->VisibleBackground;
                scene.Sky.SkyIntensity = component->SkyIntensity;
                scene.Sky.EnableDayNightCycle = component->DayNightCycle;
                scene.Sky.StarDensity = component->StarDensity;
                scene.Sky.StarIntensity = component->StarIntensity;
                scene.Sky.MilkyWayIntensity = component->MilkyWayIntensity;
                result.Environment = entity.Id;
            }
        }
    }

    if (activeCamera && result.ActiveCamera == kInvalidEntity && fallbackCamera != kInvalidEntity)
    {
        const auto* component = world.GetComponent<CameraComponent>(
            fallbackCamera, std::string(kCameraComponent));
        if (component)
        {
            ApplyCamera(world.GetWorldTransform(fallbackCamera), *component, *activeCamera);
            result.ActiveCamera = fallbackCamera;
        }
    }
    return result;
}

} // namespace engine::runtime
