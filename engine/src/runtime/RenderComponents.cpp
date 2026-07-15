#include "engine/runtime/RenderComponents.h"

#include "engine/runtime/Component.h"

namespace engine::runtime
{
namespace
{

PropertyOptions Range(std::string displayName, double minimum, double maximum,
                      double step, PropertyFlags flags = PropertyFlags::None)
{
    return {std::move(displayName), flags | PropertyFlags::HasRange,
            minimum, maximum, step, {}};
}

template <typename T>
bool Register(ComponentRegistry& registry, std::string_view name,
              std::vector<PropertyMetadata> properties, std::string* error)
{
    return registry.RegisterNative(std::string(name), 1,
        MakeDataComponentType<T>(name.data()), std::move(properties), error);
}

PropertyMetadata MaterialProperty(std::string name, std::string display,
                                  PropertyType type,
                                  std::function<PropertyValue(const Material&)> getter,
                                  std::function<bool(Material&, const PropertyValue&)> setter,
                                  PropertyOptions options = {})
{
    PropertyMetadata property;
    property.Name = std::move(name);
    property.DisplayName = display.empty() ? property.Name : std::move(display);
    property.Type = type;
    property.Flags = options.Flags;
    property.Minimum = options.Minimum;
    property.Maximum = options.Maximum;
    property.Step = options.Step;
    property.Get = [getter = std::move(getter)](const void* data) -> std::optional<PropertyValue> {
        return data ? std::optional<PropertyValue>(getter(
            static_cast<const MeshRendererComponent*>(data)->Mat)) : std::nullopt;
    };
    property.Set = [setter = std::move(setter)](void* data, const PropertyValue& value,
                                                std::string* error) {
        if (data && setter(static_cast<MeshRendererComponent*>(data)->Mat, value)) return true;
        if (error) *error = "Material property has the wrong value type";
        return false;
    };
    return property;
}

} // namespace

bool RegisterBuiltinRenderComponents(ComponentRegistry& registry, std::string* error)
{
    if (!Register<MeshRendererComponent>(registry, kMeshRendererComponent, {
        MakeProperty("MeshAsset", &MeshRendererComponent::MeshAsset,
                     {"Mesh asset", PropertyFlags::None}),
        MakeProperty("MaterialAsset", &MeshRendererComponent::MaterialAsset,
                     {"Material asset", PropertyFlags::None}),
        MaterialProperty("Albedo", "Base color", PropertyType::Vector3,
            [](const Material& m) -> PropertyValue { return m.Albedo; },
            [](Material& m, const PropertyValue& v) {
                if (const auto* value = std::get_if<glm::vec3>(&v)) { m.Albedo = *value; return true; }
                return false;
            }, {"", PropertyFlags::Color}),
        MaterialProperty("Metallic", "Metallic", PropertyType::FloatingPoint,
            [](const Material& m) -> PropertyValue { return static_cast<double>(m.Metallic); },
            [](Material& m, const PropertyValue& v) {
                if (const auto* value = std::get_if<double>(&v)) { m.Metallic = static_cast<float>(*value); return true; }
                return false;
            }, Range("", 0.0, 1.0, 0.01)),
        MaterialProperty("Roughness", "Roughness", PropertyType::FloatingPoint,
            [](const Material& m) -> PropertyValue { return static_cast<double>(m.Roughness); },
            [](Material& m, const PropertyValue& v) {
                if (const auto* value = std::get_if<double>(&v)) { m.Roughness = static_cast<float>(*value); return true; }
                return false;
            }, Range("", 0.02, 1.0, 0.01)),
        MaterialProperty("Emissive", "Emissive", PropertyType::Vector3,
            [](const Material& m) -> PropertyValue { return m.Emissive; },
            [](Material& m, const PropertyValue& v) {
                if (const auto* value = std::get_if<glm::vec3>(&v)) { m.Emissive = *value; return true; }
                return false;
            }, {"", PropertyFlags::Color}),
        MakeProperty("CastsShadows", &MeshRendererComponent::CastsShadows),
        MakeProperty("AlwaysVisible", &MeshRendererComponent::AlwaysVisible),
        MakeProperty("AllowInstancing", &MeshRendererComponent::AllowInstancing),
        MakeProperty("MaxDrawDistance", &MeshRendererComponent::MaxDrawDistance,
                     Range("Maximum draw distance", 0.0, 100000.0, 1.0))
    }, error)) return false;

    if (!Register<PointLightComponent>(registry, kPointLightComponent, {
        MakeProperty("Color", &PointLightComponent::Color, {"Color", PropertyFlags::Color}),
        MakeProperty("Intensity", &PointLightComponent::Intensity, Range("Intensity", 0.0, 1000000.0, 1.0)),
        MakeProperty("Radius", &PointLightComponent::Radius, Range("Radius", 0.01, 100000.0, 0.1)),
        MakeProperty("CastsShadows", &PointLightComponent::CastsShadows),
        MakeProperty("CookieAsset", &PointLightComponent::CookieAsset, {"Cookie asset"})
    }, error)) return false;

    if (!Register<SpotLightComponent>(registry, kSpotLightComponent, {
        MakeProperty("Color", &SpotLightComponent::Color, {"Color", PropertyFlags::Color}),
        MakeProperty("Intensity", &SpotLightComponent::Intensity, Range("Intensity", 0.0, 1000000.0, 1.0)),
        MakeProperty("Range", &SpotLightComponent::Range, Range("Range", 0.01, 100000.0, 0.1)),
        MakeProperty("InnerConeDegrees", &SpotLightComponent::InnerConeDegrees,
                     Range("Inner cone", 0.0, 89.0, 0.1, PropertyFlags::AngleDegrees)),
        MakeProperty("OuterConeDegrees", &SpotLightComponent::OuterConeDegrees,
                     Range("Outer cone", 0.1, 89.5, 0.1, PropertyFlags::AngleDegrees)),
        MakeProperty("CastsShadows", &SpotLightComponent::CastsShadows),
        MakeProperty("CookieAsset", &SpotLightComponent::CookieAsset, {"Cookie asset"})
    }, error)) return false;

    if (!Register<AreaLightComponent>(registry, kAreaLightComponent, {
        MakeProperty("Color", &AreaLightComponent::Color, {"Color", PropertyFlags::Color}),
        MakeProperty("Intensity", &AreaLightComponent::Intensity, Range("Intensity", 0.0, 1000000.0, 1.0)),
        MakeProperty("Range", &AreaLightComponent::Range, Range("Range", 0.01, 100000.0, 0.1)),
        MakeProperty("Size", &AreaLightComponent::Size, Range("Size", 0.01, 10000.0, 0.05)),
        MakeProperty("Softness", &AreaLightComponent::Softness, Range("Softness", 0.0, 1.0, 0.01)),
        MakeProperty("BarnAngleDegrees", &AreaLightComponent::BarnAngleDegrees,
                     Range("Barn angle", 1.0, 89.0, 0.1, PropertyFlags::AngleDegrees)),
        MakeProperty("MinimumRoughness", &AreaLightComponent::MinimumRoughness,
                     Range("Minimum roughness", 0.0, 1.0, 0.01)),
        MakeProperty("CastsShadows", &AreaLightComponent::CastsShadows),
        MakeProperty("CookieAsset", &AreaLightComponent::CookieAsset, {"Cookie asset"})
    }, error)) return false;

    if (!Register<DirectionalLightComponent>(registry, kDirectionalLightComponent, {
        MakeProperty("Color", &DirectionalLightComponent::Color, {"Color", PropertyFlags::Color}),
        MakeProperty("Intensity", &DirectionalLightComponent::Intensity, Range("Intensity", 0.0, 100000.0, 0.1)),
        MakeProperty("CastsShadows", &DirectionalLightComponent::CastsShadows)
    }, error)) return false;

    if (!Register<CameraComponent>(registry, kCameraComponent, {
        MakeProperty("Projection", &CameraComponent::Projection,
                     {"Projection", PropertyFlags::None, 0.0, 1.0, 1.0,
                      {"Perspective", "Orthographic"}}),
        MakeProperty("FieldOfViewDegrees", &CameraComponent::FieldOfViewDegrees,
                     Range("Field of view", 1.0, 179.0, 0.1, PropertyFlags::AngleDegrees)),
        MakeProperty("OrthographicSize", &CameraComponent::OrthographicSize,
                     Range("Orthographic size", 0.01, 100000.0, 0.1)),
        MakeProperty("NearPlane", &CameraComponent::NearPlane, Range("Near plane", 0.001, 1000.0, 0.001)),
        MakeProperty("FarPlane", &CameraComponent::FarPlane, Range("Far plane", 0.01, 1000000.0, 1.0)),
        MakeProperty("Primary", &CameraComponent::Primary)
    }, error)) return false;

    return Register<EnvironmentComponent>(registry, kEnvironmentComponent, {
        MakeProperty("Source", &EnvironmentComponent::Source,
                     {"Source", PropertyFlags::None, 0.0, 1.0, 1.0,
                      {"Procedural sky", "HDRI"}}),
        MakeProperty("HdriAsset", &EnvironmentComponent::HdriAsset, {"HDRI asset"}),
        MakeProperty("RotationDegrees", &EnvironmentComponent::RotationDegrees,
                     Range("Rotation", -360.0, 360.0, 0.1, PropertyFlags::AngleDegrees)),
        MakeProperty("ExposureEV", &EnvironmentComponent::ExposureEV, Range("IBL exposure", -20.0, 20.0, 0.05)),
        MakeProperty("BackgroundExposureEV", &EnvironmentComponent::BackgroundExposureEV,
                     Range("Background exposure", -20.0, 20.0, 0.05)),
        MakeProperty("VisibleBackground", &EnvironmentComponent::VisibleBackground),
        MakeProperty("SkyIntensity", &EnvironmentComponent::SkyIntensity, Range("Sky intensity", 0.0, 100.0, 0.01)),
        MakeProperty("DayNightCycle", &EnvironmentComponent::DayNightCycle),
        MakeProperty("StarDensity", &EnvironmentComponent::StarDensity, Range("Star density", 0.0, 0.05, 0.0001)),
        MakeProperty("StarIntensity", &EnvironmentComponent::StarIntensity, Range("Star intensity", 0.0, 100.0, 0.01)),
        MakeProperty("MilkyWayIntensity", &EnvironmentComponent::MilkyWayIntensity,
                     Range("Milky Way intensity", 0.0, 100.0, 0.01))
    }, error);
}

} // namespace engine::runtime
