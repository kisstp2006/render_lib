#include "engine/runtime/ComponentRegistry.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace engine::runtime
{

namespace
{

PropertyType ConvertType(plugin::PluginPropertyType type)
{
    using P = plugin::PluginPropertyType;
    switch (type)
    {
    case P::Boolean: return PropertyType::Boolean;
    case P::SignedInteger: return PropertyType::SignedInteger;
    case P::UnsignedInteger: return PropertyType::UnsignedInteger;
    case P::FloatingPoint: return PropertyType::FloatingPoint;
    case P::Vector2: return PropertyType::Vector2;
    case P::Vector3: return PropertyType::Vector3;
    case P::Vector4: return PropertyType::Vector4;
    case P::Quaternion: return PropertyType::Quaternion;
    case P::AssetGuid: return PropertyType::AssetGuid;
    case P::Enumeration: return PropertyType::Enumeration;
    default: return PropertyType::String;
    }
}

std::vector<std::string> SplitEnumValues(const char* values)
{
    std::vector<std::string> result;
    if (!values || values[0] == '\0')
        return result;
    std::istringstream stream(values);
    std::string value;
    while (std::getline(stream, value, ';'))
        result.push_back(std::move(value));
    return result;
}

PropertyMetadata ConvertProperty(const plugin::PluginComponentProperty& source)
{
    PropertyMetadata property;
    property.Name = source.Name ? source.Name : "";
    property.DisplayName = source.DisplayName && source.DisplayName[0] != '\0'
        ? source.DisplayName : property.Name;
    property.Type = ConvertType(source.Type);
    property.Flags = static_cast<PropertyFlags>(source.Flags);
    property.Minimum = source.Minimum;
    property.Maximum = source.Maximum;
    property.Step = source.Step;
    property.EnumValues = SplitEnumValues(source.EnumValues);
    const auto getter = source.GetText;
    const PropertyType type = property.Type;
    property.Get = [getter, type](const void* instance) -> std::optional<PropertyValue> {
        if (!getter || !instance)
            return std::nullopt;
        size_t bytes = 0;
        if (getter(instance, nullptr, &bytes) != 0 || bytes == 0 || bytes > 16 * 1024 * 1024)
            return std::nullopt;
        std::string text(bytes, '\0');
        if (getter(instance, text.data(), &bytes) != 0)
            return std::nullopt;
        text.resize(std::min(bytes, text.size()));
        if (!text.empty() && text.back() == '\0') text.pop_back();
        return PropertyValueFromString(type, text);
    };
    const auto setter = source.SetText;
    const bool readOnly = HasFlag(property.Flags, PropertyFlags::ReadOnly);
    property.Set = [setter, type, readOnly](void* instance, const PropertyValue& value,
                                            std::string* error) {
        if (!setter || !instance || readOnly)
        {
            if (error) *error = readOnly ? "Property is read-only" : "Plugin property has no setter";
            return false;
        }
        const std::string text = PropertyValueToString(type, value);
        if (setter(instance, text.c_str(), text.size()) != 0)
        {
            if (error) *error = "Plugin rejected property value";
            return false;
        }
        return true;
    };
    return property;
}

} // namespace

bool ComponentRegistry::Register(plugin::PluginId owner, const plugin::PluginComponentType& type,
                                 const plugin::PluginHostApi* host, std::string* error)
{
    if (type.StructSize < sizeof(plugin::PluginComponentType) || !type.TypeName ||
        type.TypeName[0] == '\0' || !type.Create || !type.Destroy)
    {
        if (error)
            *error = "Component registration requires a current descriptor, name, create and destroy callbacks";
        return false;
    }

    std::scoped_lock lock(m_mutex);
    if (m_types.contains(type.TypeName))
    {
        if (error)
            *error = "Component type '" + std::string(type.TypeName) + "' is already registered";
        return false;
    }

    Entry entry;
    entry.Name = type.TypeName;
    entry.Owner = owner;
    entry.Type = type;
    entry.Host = host;
    if (type.PropertyCount != 0 && !type.Properties)
    {
        if (error) *error = "Component property count is non-zero but the descriptor array is null";
        return false;
    }
    entry.Properties.reserve(type.PropertyCount);
    for (uint32_t index = 0; index < type.PropertyCount; ++index)
    {
        const plugin::PluginComponentProperty& property = type.Properties[index];
        if (property.StructSize < sizeof(plugin::PluginComponentProperty) ||
            !property.Name || property.Name[0] == '\0' || !property.GetText)
        {
            if (error) *error = "Component property descriptors require a name and getter";
            return false;
        }
        if (std::any_of(entry.Properties.begin(), entry.Properties.end(),
                        [&](const PropertyMetadata& existing) {
                            return existing.Name == property.Name;
                        }))
        {
            if (error) *error = "Duplicate property '" + std::string(property.Name) + "'";
            return false;
        }
        entry.Properties.push_back(ConvertProperty(property));
    }
    const std::string key = entry.Name;
    auto [it, inserted] = m_types.emplace(key, std::move(entry));
    if (inserted)
        it->second.Type.TypeName = it->second.Name.c_str();
    return inserted;
}

bool ComponentRegistry::RegisterNative(std::string typeName, uint32_t version,
                                       plugin::PluginComponentType callbacks,
                                       std::vector<PropertyMetadata> properties,
                                       std::string* error)
{
    callbacks.TypeName = typeName.c_str();
    callbacks.Version = version;
    callbacks.Properties = nullptr;
    callbacks.PropertyCount = 0;
    if (!Register(0, callbacks, nullptr, error))
        return false;
    std::scoped_lock lock(m_mutex);
    auto found = m_types.find(typeName);
    if (found == m_types.end())
        return false;
    found->second.Properties = std::move(properties);
    return true;
}

bool ComponentRegistry::Unregister(plugin::PluginId owner, const std::string& typeName,
                                   std::string* error)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_types.find(typeName);
    if (it == m_types.end())
        return true;
    if (it->second.Owner != owner)
    {
        if (error)
            *error = "Plugin does not own component type '" + typeName + "'";
        return false;
    }
    if (it->second.LiveInstances != 0)
    {
        if (error)
            *error = "Cannot unregister component type '" + typeName + "' while " +
                     std::to_string(it->second.LiveInstances) + " instance(s) are alive";
        return false;
    }
    m_types.erase(it);
    return true;
}

bool ComponentRegistry::UnregisterOwner(plugin::PluginId owner, std::string* error)
{
    std::scoped_lock lock(m_mutex);
    for (const auto& [name, entry] : m_types)
    {
        if (entry.Owner == owner && entry.LiveInstances != 0)
        {
            if (error)
                *error = "Cannot unload plugin while component type '" + name + "' has " +
                         std::to_string(entry.LiveInstances) + " live instance(s)";
            return false;
        }
    }
    for (auto it = m_types.begin(); it != m_types.end();)
    {
        if (it->second.Owner == owner)
            it = m_types.erase(it);
        else
            ++it;
    }
    return true;
}

bool ComponentRegistry::Create(const std::string& typeName, plugin::EntityId owner,
                               ComponentInstance& result, std::string* error)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_types.find(typeName);
    if (it == m_types.end())
    {
        if (error)
            *error = "Unknown component type '" + typeName + "'";
        return false;
    }

    void* data = it->second.Type.Create(owner, it->second.Host);
    if (!data)
    {
        if (error)
            *error = "Component factory for '" + typeName + "' failed";
        return false;
    }

    result.TypeName = typeName;
    result.Owner = it->second.Owner;
    result.Version = it->second.Type.Version;
    result.Data = data;
    result.Callbacks = it->second.Type;
    result.Callbacks.TypeName = nullptr;
    result.Enabled = true;
    result.Active = false;
    result.Started = false;
    ++it->second.LiveInstances;
    return true;
}

void ComponentRegistry::Release(ComponentInstance& instance)
{
    if (!instance.Data)
        return;
    if (instance.Active && instance.Callbacks.OnDeactivate)
        instance.Callbacks.OnDeactivate(instance.Data);
    instance.Active = false;
    if (instance.Callbacks.Destroy)
        instance.Callbacks.Destroy(instance.Data);

    std::scoped_lock lock(m_mutex);
    const auto it = m_types.find(instance.TypeName);
    if (it != m_types.end() && it->second.LiveInstances > 0)
        --it->second.LiveInstances;
    instance.Data = nullptr;
}

bool ComponentRegistry::Contains(const std::string& typeName) const
{
    std::scoped_lock lock(m_mutex);
    return m_types.contains(typeName);
}

std::vector<PropertyMetadata> ComponentRegistry::Properties(const std::string& typeName) const
{
    std::scoped_lock lock(m_mutex);
    const auto found = m_types.find(typeName);
    return found == m_types.end() ? std::vector<PropertyMetadata>{}
                                  : found->second.Properties;
}

std::optional<PropertyValue> ComponentRegistry::GetProperty(
    const std::string& typeName, const void* component, std::string_view property,
    std::string* error) const
{
    PropertyMetadata metadata;
    {
        std::scoped_lock lock(m_mutex);
        const auto type = m_types.find(typeName);
        if (type == m_types.end())
        {
            if (error) *error = "Unknown component type '" + typeName + "'";
            return std::nullopt;
        }
        const auto found = std::find_if(type->second.Properties.begin(), type->second.Properties.end(),
            [&](const PropertyMetadata& item) { return item.Name == property; });
        if (found == type->second.Properties.end())
        {
            if (error) *error = "Unknown property '" + std::string(property) + "'";
            return std::nullopt;
        }
        metadata = *found;
    }
    if (!metadata.Get)
    {
        if (error) *error = "Property has no getter";
        return std::nullopt;
    }
    auto value = metadata.Get(component);
    if (!value && error) *error = "Property getter failed";
    return value;
}

bool ComponentRegistry::SetProperty(const std::string& typeName, void* component,
                                    std::string_view property, const PropertyValue& value,
                                    std::string* error) const
{
    PropertyMetadata metadata;
    {
        std::scoped_lock lock(m_mutex);
        const auto type = m_types.find(typeName);
        if (type == m_types.end())
        {
            if (error) *error = "Unknown component type '" + typeName + "'";
            return false;
        }
        const auto found = std::find_if(type->second.Properties.begin(), type->second.Properties.end(),
            [&](const PropertyMetadata& item) { return item.Name == property; });
        if (found == type->second.Properties.end())
        {
            if (error) *error = "Unknown property '" + std::string(property) + "'";
            return false;
        }
        metadata = *found;
    }
    return metadata.Set && metadata.Set(component, value, error);
}

bool ComponentRegistry::GetPropertyText(const std::string& typeName, const void* component,
                                        std::string_view property, std::string& value,
                                        std::string* error) const
{
    const auto metadata = Properties(typeName);
    const auto found = std::find_if(metadata.begin(), metadata.end(),
        [&](const PropertyMetadata& item) { return item.Name == property; });
    if (found == metadata.end())
    {
        if (error) *error = "Unknown property '" + std::string(property) + "'";
        return false;
    }
    const auto typed = GetProperty(typeName, component, property, error);
    if (!typed) return false;
    value = PropertyValueToString(found->Type, *typed);
    return true;
}

bool ComponentRegistry::SetPropertyText(const std::string& typeName, void* component,
                                        std::string_view property, std::string_view value,
                                        std::string* error) const
{
    const auto metadata = Properties(typeName);
    const auto found = std::find_if(metadata.begin(), metadata.end(),
        [&](const PropertyMetadata& item) { return item.Name == property; });
    if (found == metadata.end())
    {
        if (error) *error = "Unknown property '" + std::string(property) + "'";
        return false;
    }
    const auto typed = PropertyValueFromString(found->Type, value, error);
    return typed && SetProperty(typeName, component, property, *typed, error);
}

size_t ComponentRegistry::LiveInstancesForOwner(plugin::PluginId owner) const
{
    std::scoped_lock lock(m_mutex);
    size_t count = 0;
    for (const auto& [name, entry] : m_types)
        if (entry.Owner == owner)
            count += entry.LiveInstances;
    return count;
}

std::vector<ComponentTypeInfo> ComponentRegistry::List() const
{
    std::scoped_lock lock(m_mutex);
    std::vector<ComponentTypeInfo> result;
    result.reserve(m_types.size());
    for (const auto& [name, entry] : m_types)
        result.push_back({name, entry.Type.Version, entry.Owner, entry.LiveInstances,
                          entry.Properties});
    return result;
}

} // namespace engine::runtime
