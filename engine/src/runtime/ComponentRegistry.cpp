#include "engine/runtime/ComponentRegistry.h"

namespace engine::runtime
{

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
    const std::string key = entry.Name;
    auto [it, inserted] = m_types.emplace(key, std::move(entry));
    if (inserted)
        it->second.Type.TypeName = it->second.Name.c_str();
    return inserted;
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
        result.push_back({name, entry.Type.Version, entry.Owner, entry.LiveInstances});
    return result;
}

} // namespace engine::runtime
