#include "engine/asset/WorldSceneSerialization.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace engine::assets
{
namespace
{

std::string Context(const SceneObjectRecord& object,
                    const SceneComponentRecord* component = nullptr,
                    std::string_view property = {})
{
    std::string result = "Entity '" + object.Name + "' (" + object.Guid.ToString() + ")";
    if (component) result += ", component '" + component->Type + "'";
    if (!property.empty()) result += ", property '" + std::string(property) + "'";
    return result;
}

} // namespace

bool SerializeWorld(runtime::World& world, SceneAssetData& scene,
                    runtime::EntityId activeCamera, std::string* error)
{
    SceneAssetData result;
    result.FormatVersion = 1;
    result.Layers = scene.Layers;
    result.Environment = scene.Environment;
    result.Lighting = scene.Lighting;
    result.EditorMetadata = scene.EditorMetadata;
    result.Skybox = scene.Skybox;
    if (activeCamera != runtime::kInvalidEntity)
        result.ActiveCameraObject = world.GetGuid(activeCamera);

    for (const runtime::EntityInfo& entity : world.ListEntities())
    {
        SceneObjectRecord object;
        object.Guid = entity.Guid;
        object.Parent = world.GetGuid(entity.Parent);
        object.Name = entity.Name;
        object.Tag = entity.Tag;
        object.Layer = entity.Layer;
        object.Active = entity.ActiveSelf;
        const runtime::Transform transform = world.GetLocalTransform(entity.Id);
        object.Position = transform.Position;
        object.Rotation = transform.Rotation;
        object.Scale = transform.Scale;

        for (const runtime::ConstComponentView& component :
             static_cast<const runtime::World&>(world).ListComponents(entity.Id))
        {
            SceneComponentRecord serialized;
            serialized.Type = component.TypeName;
            serialized.Version = component.Version;
            serialized.Enabled = component.Enabled;
            const auto properties = world.Components().Properties(component.TypeName);
            for (const runtime::PropertyMetadata& property : properties)
            {
                if (runtime::HasFlag(property.Flags, runtime::PropertyFlags::Transient))
                    continue;
                const auto value = property.Get ? property.Get(component.Data) : std::nullopt;
                if (!value)
                {
                    if (error) *error = Context(object, &serialized, property.Name) +
                        " could not be read";
                    return false;
                }
                serialized.Properties[property.Name] =
                    runtime::PropertyValueToString(property.Type, *value);
            }
            object.Components.push_back(std::move(serialized));
        }
        result.Objects.push_back(std::move(object));
    }
    scene = std::move(result);
    return true;
}

bool DeserializeWorld(const SceneAssetData& scene, runtime::World& world,
                      runtime::EntityId* activeCamera, std::string* error)
{
    std::unordered_set<AssetGuid, AssetGuidHash> guids;
    std::unordered_map<AssetGuid, const SceneObjectRecord*, AssetGuidHash> objects;
    for (const SceneObjectRecord& object : scene.Objects)
    {
        if (!object.Guid.IsValid() || !guids.insert(object.Guid).second)
        {
            if (error) *error = "Scene contains an invalid or duplicate entity GUID";
            return false;
        }
        objects.emplace(object.Guid, &object);
    }
    for (const SceneObjectRecord& object : scene.Objects)
    {
        if (object.Parent.IsValid() && !objects.contains(object.Parent))
        {
            if (error) *error = Context(object) + " references a missing parent GUID";
            return false;
        }
        for (const SceneComponentRecord& component : object.Components)
        {
            if (!world.Components().Contains(component.Type))
            {
                if (error) *error = Context(object, &component) + " is not registered";
                return false;
            }
            const auto metadata = world.Components().Properties(component.Type);
            for (const auto& [name, text] : component.Properties)
            {
                const auto property = std::find_if(metadata.begin(), metadata.end(),
                    [&](const runtime::PropertyMetadata& item) { return item.Name == name; });
                if (property == metadata.end())
                {
                    if (error) *error = Context(object, &component, name) + " is unknown";
                    return false;
                }
                std::string parseError;
                if (!runtime::PropertyValueFromString(property->Type, text, &parseError))
                {
                    if (error) *error = Context(object, &component, name) + ": " + parseError;
                    return false;
                }
            }
        }
    }

    world.Clear();
    std::unordered_map<AssetGuid, runtime::EntityId, AssetGuidHash> entityIds;
    for (const SceneObjectRecord& object : scene.Objects)
    {
        const runtime::EntityId entity = world.CreateEntityWithGuid(object.Guid, object.Name);
        if (entity == runtime::kInvalidEntity)
        {
            if (error) *error = Context(object) + " could not be created";
            world.Clear();
            return false;
        }
        entityIds.emplace(object.Guid, entity);
        world.SetTag(entity, object.Tag);
        world.SetLayer(entity, object.Layer);
        world.SetLocalTransform(entity, {object.Position, object.Rotation, object.Scale});
        world.SetActive(entity, object.Active);
    }
    for (const SceneObjectRecord& object : scene.Objects)
    {
        const runtime::EntityId entity = entityIds.at(object.Guid);
        if (object.Parent.IsValid())
            world.SetParent(entity, entityIds.at(object.Parent));
        for (const SceneComponentRecord& component : object.Components)
        {
            std::string componentError;
            if (!world.AddComponent(entity, component.Type, &componentError))
            {
                if (error) *error = Context(object, &component) + ": " + componentError;
                world.Clear();
                return false;
            }
            void* data = world.GetComponentData(entity, component.Type);
            for (const auto& [name, text] : component.Properties)
            {
                if (!world.Components().SetPropertyText(component.Type, data, name, text,
                                                        &componentError))
                {
                    if (error) *error = Context(object, &component, name) + ": " + componentError;
                    world.Clear();
                    return false;
                }
            }
            world.SetComponentEnabled(entity, component.Type, component.Enabled);
        }
    }
    if (activeCamera)
    {
        const auto found = entityIds.find(scene.ActiveCameraObject);
        *activeCamera = found == entityIds.end() ? runtime::kInvalidEntity
                                                  : found->second;
    }
    return true;
}

bool SaveWorldScene(const std::filesystem::path& path, runtime::World& world,
                    runtime::EntityId activeCamera, std::string* error)
{
    AssetDescriptor descriptor;
    std::error_code existsError;
    if (std::filesystem::exists(path, existsError))
    {
        if (!LoadAssetDescriptor(path, descriptor, error))
            return false;
        if (!descriptor.Type.empty() && descriptor.Type != kSceneAssetType)
        {
            if (error) *error = "Cannot overwrite non-scene asset descriptor: " +
                                path.string();
            return false;
        }
    }
    if (existsError)
    {
        if (error) *error = "Cannot inspect scene path: " + existsError.message();
        return false;
    }

    SceneAssetData scene;
    if (!SerializeWorld(world, scene, activeCamera, error))
        return false;
    if (!descriptor.Guid.IsValid())
        descriptor.Guid = AssetGuid::Generate();
    descriptor.Type = std::string(kSceneAssetType);
    if (descriptor.Name.empty())
        descriptor.Name = path.stem().string();
    descriptor.DescriptorVersion = 1;
    WriteSceneAssetData(descriptor, scene);
    return SaveAssetDescriptor(path, descriptor, error);
}

bool LoadWorldScene(const std::filesystem::path& path, runtime::World& world,
                    runtime::EntityId* activeCamera, std::string* error)
{
    AssetDescriptor descriptor;
    if (!LoadAssetDescriptor(path, descriptor, error))
        return false;
    if (descriptor.Type != kSceneAssetType)
    {
        if (error) *error = "Asset descriptor is not a scene: " + path.string();
        return false;
    }
    SceneAssetData scene;
    if (!ReadSceneAssetData(descriptor, scene, error))
        return false;
    return DeserializeWorld(scene, world, activeCamera, error);
}

} // namespace engine::assets
