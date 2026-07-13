#include "engine/plugin/ServiceRegistry.h"

namespace engine::plugin
{

bool ServiceRegistry::Register(PluginId owner, std::string name, uint32_t version, void* service,
                               std::string* error)
{
    if (name.empty() || !service)
    {
        if (error)
            *error = "Service registration requires a non-empty name and non-null pointer";
        return false;
    }
    std::scoped_lock lock(m_mutex);
    if (m_services.contains(name))
    {
        if (error)
            *error = "A service named '" + name + "' is already registered";
        return false;
    }
    ServiceInfo info;
    info.Name = std::move(name);
    info.Version = version;
    info.Owner = owner;
    info.Service = service;
    m_services.emplace(info.Name, std::move(info));
    return true;
}

bool ServiceRegistry::Unregister(PluginId owner, const std::string& name, std::string* error)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_services.find(name);
    if (it == m_services.end())
        return true;
    if (it->second.Owner != owner)
    {
        if (error)
            *error = "Plugin does not own service '" + name + "'";
        return false;
    }
    m_services.erase(it);
    return true;
}

void ServiceRegistry::UnregisterOwner(PluginId owner)
{
    std::scoped_lock lock(m_mutex);
    for (auto it = m_services.begin(); it != m_services.end();)
    {
        if (it->second.Owner == owner)
            it = m_services.erase(it);
        else
            ++it;
    }
}

void* ServiceRegistry::Find(const std::string& name, uint32_t minimumVersion) const
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_services.find(name);
    if (it == m_services.end() || it->second.Version < minimumVersion)
        return nullptr;
    return it->second.Service;
}

std::vector<ServiceInfo> ServiceRegistry::List() const
{
    std::scoped_lock lock(m_mutex);
    std::vector<ServiceInfo> result;
    result.reserve(m_services.size());
    for (const auto& [name, service] : m_services)
        result.push_back(service);
    return result;
}

} // namespace engine::plugin
