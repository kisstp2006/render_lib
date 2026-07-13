#pragma once

#include "engine/plugin/PluginApi.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::runtime
{

struct ComponentTypeInfo
{
    std::string TypeName;
    uint32_t Version = 0;
    plugin::PluginId Owner = 0;
    size_t LiveInstances = 0;
};

struct ComponentInstance
{
    std::string TypeName;
    plugin::PluginId Owner = 0;
    void* Data = nullptr;
    plugin::PluginComponentType Callbacks;
    bool Enabled = true;
    bool Active = false;
    bool Started = false;
};

class ComponentRegistry
{
  public:
    bool Register(plugin::PluginId owner, const plugin::PluginComponentType& type,
                  const plugin::PluginHostApi* host, std::string* error = nullptr);
    bool Unregister(plugin::PluginId owner, const std::string& typeName,
                    std::string* error = nullptr);
    bool UnregisterOwner(plugin::PluginId owner, std::string* error = nullptr);

    bool Create(const std::string& typeName, plugin::EntityId owner, ComponentInstance& result,
                std::string* error = nullptr);
    void Release(ComponentInstance& instance);

    bool Contains(const std::string& typeName) const;
    size_t LiveInstancesForOwner(plugin::PluginId owner) const;
    std::vector<ComponentTypeInfo> List() const;

  private:
    struct Entry
    {
        std::string Name;
        plugin::PluginId Owner = 0;
        plugin::PluginComponentType Type;
        const plugin::PluginHostApi* Host = nullptr;
        size_t LiveInstances = 0;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Entry> m_types;
};

} // namespace engine::runtime
