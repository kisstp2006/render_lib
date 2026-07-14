#include "engine/asset/SceneAsset.h"

#include "engine/resource/BinaryIO.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace engine::assets
{
namespace
{

AssetDiagnostic Diagnostic(AssetDiagnosticSeverity severity, std::string code, std::string message)
{
    AssetDiagnostic diagnostic;
    diagnostic.Severity = severity;
    diagnostic.Code = std::move(code);
    diagnostic.Message = std::move(message);
    diagnostic.Step = "scene export";
    return diagnostic;
}

std::string Bool(bool value)
{
    return value ? "true" : "false";
}
std::string Float(float value)
{
    return std::to_string(value);
}
std::string Vec3(glm::vec3 value)
{
    return Float(value.x) + ',' + Float(value.y) + ',' + Float(value.z);
}
std::string Quat(glm::quat value)
{
    return Float(value.w) + ',' + Float(value.x) + ',' + Float(value.y) + ',' + Float(value.z);
}
std::string GuidText(AssetGuid guid)
{
    return guid.IsValid() ? guid.ToString() : std::string{};
}

bool ParseBool(std::string_view text, bool &value)
{
    if (text == "true" || text == "1")
    {
        value = true;
        return true;
    }
    if (text == "false" || text == "0")
    {
        value = false;
        return true;
    }
    return false;
}

bool ParseFloats(std::string_view text, std::span<float> output)
{
    std::string copy(text);
    char *cursor = copy.data();
    for (size_t index = 0; index < output.size(); ++index)
    {
        char *end = nullptr;
        const float value = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(value))
            return false;
        output[index] = value;
        if (index + 1 < output.size())
        {
            if (*end != ',')
                return false;
            cursor = end + 1;
        }
        else if (*end != '\0')
            return false;
    }
    return true;
}

bool ParseU32(std::string_view text, uint32_t &value)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

std::string Key(size_t object, std::string_view field)
{
    return "scene.object." + std::to_string(object) + '.' + std::string(field);
}

void PutMap(std::map<std::string, std::string> &settings, std::string_view prefix,
            const std::map<std::string, std::string> &values)
{
    settings[std::string(prefix) + ".count"] = std::to_string(values.size());
    size_t index = 0;
    for (const auto &[key, value] : values)
    {
        settings[std::string(prefix) + '.' + std::to_string(index) + ".key"] = key;
        settings[std::string(prefix) + '.' + std::to_string(index) + ".value"] = value;
        ++index;
    }
}

bool GetMap(const std::map<std::string, std::string> &settings, std::string_view prefix,
            std::map<std::string, std::string> &values, std::string *error)
{
    const auto countIt = settings.find(std::string(prefix) + ".count");
    uint32_t count = 0;
    if (countIt == settings.end() || !ParseU32(countIt->second, count) || count > 100000)
    {
        if (error)
            *error = "Invalid scene map count: " + std::string(prefix);
        return false;
    }
    values.clear();
    for (uint32_t index = 0; index < count; ++index)
    {
        const std::string base = std::string(prefix) + '.' + std::to_string(index);
        const auto key = settings.find(base + ".key"), value = settings.find(base + ".value");
        if (key == settings.end() || value == settings.end() || !values.emplace(key->second, value->second).second)
        {
            if (error)
                *error = "Malformed or duplicate scene map entry";
            return false;
        }
    }
    return true;
}

std::vector<AssetGuid> CollectDependencies(const SceneAssetData &scene)
{
    std::vector<AssetGuid> dependencies;
    if (scene.Skybox)
        dependencies.push_back(scene.Skybox.Guid);
    for (const auto &object : scene.Objects)
    {
        if (object.Prefab.IsValid())
            dependencies.push_back(object.Prefab);
        dependencies.insert(dependencies.end(), object.AssetReferences.begin(), object.AssetReferences.end());
    }
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    return dependencies;
}

void WriteGuid(resources::BinaryWriter &writer, AssetGuid guid)
{
    writer.WriteU64(guid.High);
    writer.WriteU64(guid.Low);
}
bool ReadGuid(resources::BinaryReader &reader, AssetGuid &guid)
{
    return reader.ReadU64(guid.High) && reader.ReadU64(guid.Low);
}

std::vector<std::byte> EncodeScene(const SceneAssetData &scene)
{
    resources::BinaryWriter writer;
    writer.WriteU32(1);
    writer.WriteU32(scene.FormatVersion);
    writer.WriteU32(static_cast<uint32_t>(scene.Objects.size()));
    for (const auto &object : scene.Objects)
    {
        WriteGuid(writer, object.Guid);
        WriteGuid(writer, object.Parent);
        writer.WriteString(object.Name);
        writer.WriteString(object.Tag);
        writer.WriteU32(object.Layer);
        writer.WriteU8(object.Active);
        writer.WriteU8(0);
        writer.WriteU16(0);
        for (float value :
             {object.Position.x, object.Position.y, object.Position.z, object.Rotation.w, object.Rotation.x,
              object.Rotation.y, object.Rotation.z, object.Scale.x, object.Scale.y, object.Scale.z})
            writer.WriteF32(value);
        WriteGuid(writer, object.Prefab);
        writer.WriteU32(static_cast<uint32_t>(object.AssetReferences.size()));
        for (AssetGuid reference : object.AssetReferences)
            WriteGuid(writer, reference);
        writer.WriteU32(static_cast<uint32_t>(object.Components.size()));
        for (const auto &component : object.Components)
        {
            writer.WriteString(component.Type);
            writer.WriteU32(component.Version);
            writer.WriteU8(component.Enabled);
            writer.WriteU32(static_cast<uint32_t>(component.Properties.size()));
            for (const auto &[key, value] : component.Properties)
            {
                writer.WriteString(key);
                writer.WriteString(value);
            }
        }
    }
    WriteGuid(writer, scene.Skybox.Guid);
    WriteGuid(writer, scene.ActiveCameraObject);
    writer.WriteU32(static_cast<uint32_t>(scene.Layers.size()));
    for (const auto &layer : scene.Layers)
        writer.WriteString(layer);
    auto writeMap = [&](const auto &values) {
        writer.WriteU32(static_cast<uint32_t>(values.size()));
        for (const auto &[key, value] : values)
        {
            writer.WriteString(key);
            writer.WriteString(value);
        }
    };
    writeMap(scene.Environment);
    writeMap(scene.Lighting);
    return writer.TakeData();
}

std::shared_ptr<SceneAssetData> LoadScene(const resources::ResourceLoadContext &context, std::string *error)
{
    resources::BinaryReader reader(context.Payload);
    uint32_t resourceVersion = 0, objectCount = 0;
    auto scene = std::make_shared<SceneAssetData>();
    if (!reader.ReadU32(resourceVersion) || resourceVersion != 1 || !reader.ReadU32(scene->FormatVersion) ||
        !reader.ReadU32(objectCount) || objectCount > 1000000)
    {
        if (error)
            *error = "Malformed scene resource header";
        return {};
    }
    scene->Objects.reserve(objectCount);
    for (uint32_t index = 0; index < objectCount; ++index)
    {
        SceneObjectRecord object;
        uint8_t active = 0, reserved = 0;
        uint16_t reserved16 = 0;
        if (!ReadGuid(reader, object.Guid) || !ReadGuid(reader, object.Parent) ||
            !reader.ReadString(object.Name, 65536) || !reader.ReadString(object.Tag, 65536) ||
            !reader.ReadU32(object.Layer) || !reader.ReadU8(active) || !reader.ReadU8(reserved) ||
            !reader.ReadU16(reserved16))
        {
            if (error)
                *error = "Malformed scene object";
            return {};
        }
        object.Active = active != 0;
        float *transforms[] = {&object.Position.x, &object.Position.y, &object.Position.z, &object.Rotation.w,
                               &object.Rotation.x, &object.Rotation.y, &object.Rotation.z, &object.Scale.x,
                               &object.Scale.y,    &object.Scale.z};
        for (float *value : transforms)
            if (!reader.ReadF32(*value))
            {
                if (error)
                    *error = "Truncated scene transform";
                return {};
            }
        if (!ReadGuid(reader, object.Prefab))
        {
            if (error)
                *error = "Truncated prefab reference";
            return {};
        }
        uint32_t referenceCount = 0;
        if (!reader.ReadU32(referenceCount) || referenceCount > 100000)
        {
            if (error)
                *error = "Invalid scene asset reference count";
            return {};
        }
        object.AssetReferences.resize(referenceCount);
        for (auto &reference : object.AssetReferences)
            if (!ReadGuid(reader, reference))
            {
                if (error)
                    *error = "Truncated asset reference";
                return {};
            }
        uint32_t componentCount = 0;
        if (!reader.ReadU32(componentCount) || componentCount > 10000)
        {
            if (error)
                *error = "Invalid scene component count";
            return {};
        }
        object.Components.reserve(componentCount);
        for (uint32_t componentIndex = 0; componentIndex < componentCount; ++componentIndex)
        {
            SceneComponentRecord component;
            uint8_t enabled = 0;
            uint32_t propertyCount = 0;
            if (!reader.ReadString(component.Type, 4096) || !reader.ReadU32(component.Version) ||
                !reader.ReadU8(enabled) || !reader.ReadU32(propertyCount) || propertyCount > 100000)
            {
                if (error)
                    *error = "Malformed scene component";
                return {};
            }
            component.Enabled = enabled != 0;
            for (uint32_t propertyIndex = 0; propertyIndex < propertyCount; ++propertyIndex)
            {
                std::string key, value;
                if (!reader.ReadString(key, 65536) || !reader.ReadString(value, 1024 * 1024) ||
                    !component.Properties.emplace(std::move(key), std::move(value)).second)
                {
                    if (error)
                        *error = "Malformed component property";
                    return {};
                }
            }
            object.Components.push_back(std::move(component));
        }
        scene->Objects.push_back(std::move(object));
    }
    if (!ReadGuid(reader, scene->Skybox.Guid) || !ReadGuid(reader, scene->ActiveCameraObject))
    {
        if (error)
            *error = "Truncated scene settings";
        return {};
    }
    uint32_t layerCount = 0;
    if (!reader.ReadU32(layerCount) || layerCount > 4096)
    {
        if (error)
            *error = "Invalid scene layer count";
        return {};
    }
    for (uint32_t index = 0; index < layerCount; ++index)
    {
        std::string layer;
        if (!reader.ReadString(layer, 4096))
        {
            if (error)
                *error = reader.Error();
            return {};
        }
        scene->Layers.push_back(std::move(layer));
    }
    auto readMap = [&](auto &values) {
        uint32_t count = 0;
        if (!reader.ReadU32(count) || count > 100000)
            return false;
        for (uint32_t index = 0; index < count; ++index)
        {
            std::string key, value;
            if (!reader.ReadString(key, 65536) || !reader.ReadString(value, 1024 * 1024) ||
                !values.emplace(std::move(key), std::move(value)).second)
                return false;
        }
        return true;
    };
    if (!readMap(scene->Environment) || !readMap(scene->Lighting) || reader.Remaining() != 0)
    {
        if (error)
            *error = reader.Error().empty() ? "Malformed scene maps or trailing data" : reader.Error();
        return {};
    }
    return scene;
}

} // namespace

void WriteSceneAssetData(AssetDescriptor &descriptor, const SceneAssetData &scene)
{
    auto &settings = descriptor.Settings;
    for (auto it = settings.begin(); it != settings.end();)
        if (it->first.starts_with("scene."))
            it = settings.erase(it);
        else
            ++it;
    settings["scene.format_version"] = std::to_string(scene.FormatVersion);
    settings["scene.object.count"] = std::to_string(scene.Objects.size());
    for (size_t objectIndex = 0; objectIndex < scene.Objects.size(); ++objectIndex)
    {
        const auto &object = scene.Objects[objectIndex];
        settings[Key(objectIndex, "guid")] = GuidText(object.Guid);
        settings[Key(objectIndex, "parent")] = GuidText(object.Parent);
        settings[Key(objectIndex, "name")] = object.Name;
        settings[Key(objectIndex, "tag")] = object.Tag;
        settings[Key(objectIndex, "layer")] = std::to_string(object.Layer);
        settings[Key(objectIndex, "active")] = Bool(object.Active);
        settings[Key(objectIndex, "position")] = Vec3(object.Position);
        settings[Key(objectIndex, "rotation")] = Quat(object.Rotation);
        settings[Key(objectIndex, "scale")] = Vec3(object.Scale);
        settings[Key(objectIndex, "prefab")] = GuidText(object.Prefab);
        settings[Key(objectIndex, "asset.count")] = std::to_string(object.AssetReferences.size());
        for (size_t index = 0; index < object.AssetReferences.size(); ++index)
            settings[Key(objectIndex, "asset." + std::to_string(index))] = GuidText(object.AssetReferences[index]);
        settings[Key(objectIndex, "component.count")] = std::to_string(object.Components.size());
        for (size_t componentIndex = 0; componentIndex < object.Components.size(); ++componentIndex)
        {
            const auto &component = object.Components[componentIndex];
            const std::string prefix = Key(objectIndex, "component." + std::to_string(componentIndex));
            settings[prefix + ".type"] = component.Type;
            settings[prefix + ".version"] = std::to_string(component.Version);
            settings[prefix + ".enabled"] = Bool(component.Enabled);
            PutMap(settings, prefix + ".property", component.Properties);
        }
    }
    settings["scene.skybox"] = GuidText(scene.Skybox.Guid);
    settings["scene.active_camera"] = GuidText(scene.ActiveCameraObject);
    settings["scene.layer.count"] = std::to_string(scene.Layers.size());
    for (size_t index = 0; index < scene.Layers.size(); ++index)
        settings["scene.layer." + std::to_string(index)] = scene.Layers[index];
    PutMap(settings, "scene.environment", scene.Environment);
    PutMap(settings, "scene.lighting", scene.Lighting);
    PutMap(settings, "scene.editor", scene.EditorMetadata);
    descriptor.AssetDependencies = CollectDependencies(scene);
}

bool ReadSceneAssetData(const AssetDescriptor &descriptor, SceneAssetData &scene, std::string *error)
{
    const auto &settings = descriptor.Settings;
    auto required = [&](const std::string &key) -> const std::string * {
        const auto found = settings.find(key);
        if (found == settings.end())
        {
            if (error)
                *error = "Scene descriptor is missing key '" + key + "'";
            return nullptr;
        }
        return &found->second;
    };
    SceneAssetData result;
    const std::string *value = required("scene.format_version");
    if (!value || !ParseU32(*value, result.FormatVersion))
        return false;
    uint32_t objectCount = 0;
    value = required("scene.object.count");
    if (!value || !ParseU32(*value, objectCount) || objectCount > 1000000)
    {
        if (error)
            *error = "Invalid scene object count";
        return false;
    }
    result.Objects.reserve(objectCount);
    for (uint32_t objectIndex = 0; objectIndex < objectCount; ++objectIndex)
    {
        SceneObjectRecord object;
        auto parseGuid = [&](std::string_view field, AssetGuid &guid) {
            const std::string *text = required(Key(objectIndex, field));
            if (!text)
                return false;
            if (text->empty())
                return true;
            const auto parsed = AssetGuid::Parse(*text);
            if (!parsed)
            {
                if (error)
                    *error = "Invalid object GUID in " + Key(objectIndex, field);
                return false;
            }
            guid = *parsed;
            return true;
        };
        if (!parseGuid("guid", object.Guid) || !object.Guid.IsValid() || !parseGuid("parent", object.Parent) ||
            !parseGuid("prefab", object.Prefab))
            return false;
        value = required(Key(objectIndex, "name"));
        if (!value)
            return false;
        object.Name = *value;
        value = required(Key(objectIndex, "tag"));
        if (!value)
            return false;
        object.Tag = *value;
        value = required(Key(objectIndex, "layer"));
        if (!value || !ParseU32(*value, object.Layer))
            return false;
        value = required(Key(objectIndex, "active"));
        if (!value || !ParseBool(*value, object.Active))
            return false;
        float position[3], rotation[4], scale[3];
        value = required(Key(objectIndex, "position"));
        if (!value || !ParseFloats(*value, position))
            return false;
        value = required(Key(objectIndex, "rotation"));
        if (!value || !ParseFloats(*value, rotation))
            return false;
        value = required(Key(objectIndex, "scale"));
        if (!value || !ParseFloats(*value, scale))
            return false;
        object.Position = {position[0], position[1], position[2]};
        object.Rotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
        object.Scale = {scale[0], scale[1], scale[2]};
        uint32_t assetCount = 0;
        value = required(Key(objectIndex, "asset.count"));
        if (!value || !ParseU32(*value, assetCount) || assetCount > 100000)
            return false;
        for (uint32_t index = 0; index < assetCount; ++index)
        {
            value = required(Key(objectIndex, "asset." + std::to_string(index)));
            if (!value)
                return false;
            const auto guid = AssetGuid::Parse(*value);
            if (!guid)
                return false;
            object.AssetReferences.push_back(*guid);
        }
        uint32_t componentCount = 0;
        value = required(Key(objectIndex, "component.count"));
        if (!value || !ParseU32(*value, componentCount) || componentCount > 10000)
            return false;
        for (uint32_t componentIndex = 0; componentIndex < componentCount; ++componentIndex)
        {
            const std::string prefix = Key(objectIndex, "component." + std::to_string(componentIndex));
            SceneComponentRecord component;
            value = required(prefix + ".type");
            if (!value)
                return false;
            component.Type = *value;
            value = required(prefix + ".version");
            if (!value || !ParseU32(*value, component.Version))
                return false;
            value = required(prefix + ".enabled");
            if (!value || !ParseBool(*value, component.Enabled) ||
                !GetMap(settings, prefix + ".property", component.Properties, error))
                return false;
            object.Components.push_back(std::move(component));
        }
        result.Objects.push_back(std::move(object));
    }
    value = required("scene.skybox");
    if (!value)
        return false;
    if (const auto guid = AssetGuid::Parse(*value))
        result.Skybox.Guid = *guid;
    value = required("scene.active_camera");
    if (!value)
        return false;
    if (const auto guid = AssetGuid::Parse(*value))
        result.ActiveCameraObject = *guid;
    uint32_t layerCount = 0;
    value = required("scene.layer.count");
    if (!value || !ParseU32(*value, layerCount) || layerCount > 4096)
        return false;
    for (uint32_t index = 0; index < layerCount; ++index)
    {
        value = required("scene.layer." + std::to_string(index));
        if (!value)
            return false;
        result.Layers.push_back(*value);
    }
    if (!GetMap(settings, "scene.environment", result.Environment, error) ||
        !GetMap(settings, "scene.lighting", result.Lighting, error) ||
        !GetMap(settings, "scene.editor", result.EditorMetadata, error))
        return false;
    scene = std::move(result);
    return true;
}

bool ValidateSceneReferences(const SceneAssetData &scene, const AssetDatabase &database,
                             const SceneComponentTypeQuery &componentTypeExists,
                             std::vector<AssetDiagnostic> &diagnostics)
{
    std::unordered_map<AssetGuid, const SceneObjectRecord *, AssetGuidHash> objects;
    for (const auto &object : scene.Objects)
    {
        if (!object.Guid.IsValid() || !objects.emplace(object.Guid, &object).second)
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_duplicate_object_guid",
                                             "Scene contains an invalid or duplicate object GUID."));
    }
    for (const auto &object : scene.Objects)
    {
        if (object.Parent.IsValid() && !objects.contains(object.Parent))
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_parent_missing",
                                             "Object '" + object.Name + "' references a missing parent."));
        for (AssetGuid reference : object.AssetReferences)
            if (!database.Find(reference))
                diagnostics.push_back(
                    Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_asset_missing",
                               "Object '" + object.Name + "' references missing asset " + reference.ToString()));
        if (object.Prefab.IsValid() && !database.Find(object.Prefab))
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_prefab_missing",
                                             "Object '" + object.Name + "' references a missing prefab."));
        for (const auto &component : object.Components)
        {
            if (component.Type.empty() || component.Version == 0)
                diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_component_invalid",
                                                 "Object '" + object.Name + "' has invalid component metadata."));
            else if (componentTypeExists && !componentTypeExists(component.Type, component.Version))
                diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_component_unknown",
                                                 "Unknown component '" + component.Type + "' version " +
                                                     std::to_string(component.Version) + "."));
        }
    }
    if (scene.Skybox && !database.Find(scene.Skybox.Guid))
        diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_skybox_missing",
                                         "Scene references a missing skybox asset."));
    if (scene.ActiveCameraObject.IsValid() && !objects.contains(scene.ActiveCameraObject))
        diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_camera_missing",
                                         "Active camera object does not exist."));
    enum class Mark : uint8_t
    {
        None,
        Visiting,
        Done
    };
    std::unordered_map<AssetGuid, Mark, AssetGuidHash> marks;
    std::function<bool(AssetGuid)> visit = [&](AssetGuid guid) {
        Mark &mark = marks[guid];
        if (mark == Mark::Visiting)
            return false;
        if (mark == Mark::Done)
            return true;
        mark = Mark::Visiting;
        const auto found = objects.find(guid);
        if (found != objects.end() && found->second->Parent.IsValid() && !visit(found->second->Parent))
            return false;
        mark = Mark::Done;
        return true;
    };
    for (const auto &[guid, unused] : objects)
        if (!visit(guid))
        {
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_hierarchy_cycle",
                                             "Scene parent hierarchy contains a cycle."));
            break;
        }
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const AssetDiagnostic &diagnostic) {
        return diagnostic.Severity == AssetDiagnosticSeverity::FatalError;
    });
}

bool CreateSceneAsset(AssetPipeline &pipeline, const SceneAssetData &scene, std::string_view name,
                      const std::filesystem::path &descriptorPath, AssetGuid *createdGuid, std::string *error)
{
    AssetDescriptor descriptor;
    descriptor.Guid = AssetGuid::Generate();
    descriptor.Type = std::string(kSceneAssetType);
    descriptor.Name = std::string(name);
    descriptor.State = AssetImportState::Dirty;
    WriteSceneAssetData(descriptor, scene);
    std::vector<AssetDiagnostic> diagnostics;
    if (!ValidateSceneReferences(scene, pipeline.Database(), {}, diagnostics))
    {
        if (error)
            *error = diagnostics.front().Message;
        return false;
    }
    if (!SaveAssetDescriptor(descriptorPath, descriptor, error) ||
        !pipeline.Database().AddOrUpdate(descriptorPath, descriptor, error))
        return false;
    if (createdGuid)
        *createdGuid = descriptor.Guid;
    return true;
}

bool RegisterSceneAssetType(AssetTypeRegistry &registry, resources::ResourceManager &resourceManager,
                            SceneComponentTypeQuery componentTypeExists, std::string *error)
{
    AssetTypeRegistration type;
    type.TypeId = std::string(kSceneAssetType);
    type.DisplayName = "Scene";
    type.DescriptorExtension = std::string(kSceneDescriptorExtension);
    type.RuntimeType = std::string(kSceneResourceType);
    type.Icon = "scene";
    type.Properties = {{"scene.skybox",
                        "Skybox",
                        "Environment",
                        AssetPropertyType::AssetReference,
                        "",
                        {},
                        {},
                        {},
                        std::string(kSkyboxAssetType)},
                       {"scene.active_camera", "Active Camera Object", "Scene", AssetPropertyType::String, ""}};
    type.Validate = [componentTypeExists](const AssetDescriptor &descriptor, const AssetValidationContext &) {
        std::vector<AssetDiagnostic> diagnostics;
        SceneAssetData scene;
        std::string error;
        if (!ReadSceneAssetData(descriptor, scene, &error))
            diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_parse_failed", error));
        else
        {
            std::unordered_set<AssetGuid, AssetGuidHash> guids;
            for (const auto &object : scene.Objects)
            {
                if (!object.Guid.IsValid() || !guids.insert(object.Guid).second)
                    diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_duplicate_object_guid",
                                                     "Scene contains duplicate object GUIDs."));
                for (const auto &component : object.Components)
                    if (componentTypeExists && !componentTypeExists(component.Type, component.Version))
                        diagnostics.push_back(Diagnostic(AssetDiagnosticSeverity::FatalError, "scene_component_unknown",
                                                         "Unknown component '" + component.Type + "'."));
            }
        }
        return diagnostics;
    };
    type.Transform = [](const AssetDescriptor &descriptor, const AssetTransformContext &, AssetTransformOutput &output,
                        std::string *transformError) {
        SceneAssetData scene;
        if (!ReadSceneAssetData(descriptor, scene, transformError))
            return false;
        output.RuntimeType = std::string(kSceneResourceType);
        output.ResourceVersion = 1;
        output.Compression = "rle";
        output.Payload = EncodeScene(scene);
        output.AssetDependencies = CollectDependencies(scene);
        output.Statistics["objects"] = scene.Objects.size();
        uint64_t components = 0;
        for (const auto &object : scene.Objects)
            components += object.Components.size();
        output.Statistics["components"] = components;
        output.Statistics["payload_bytes"] = output.Payload.size();
        return true;
    };
    type.Inspector = [](const AssetDescriptor &) {
        return std::vector<AssetPropertySchema>{
            {"scene.skybox",
             "Skybox",
             "Environment",
             AssetPropertyType::AssetReference,
             "",
             {},
             {},
             {},
             std::string(kSkyboxAssetType)},
            {"scene.active_camera", "Active Camera Object", "Scene", AssetPropertyType::String, ""}};
    };
    if (!registry.Register(std::move(type), error))
        return false;
    return resourceManager.RegisterLoader<SceneAssetData>(std::string(kSceneResourceType), LoadScene, error);
}

} // namespace engine::assets
