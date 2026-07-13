#pragma once

#include "engine/plugin/PluginApi.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::plugin
{

struct ServiceInfo
{
    std::string Name;
    uint32_t Version = 0;
    PluginId Owner = 0;
    void* Service = nullptr;
};

class ServiceRegistry
{
  public:
    bool Register(PluginId owner, std::string name, uint32_t version, void* service,
                  std::string* error = nullptr);
    bool Unregister(PluginId owner, const std::string& name, std::string* error = nullptr);
    void UnregisterOwner(PluginId owner);
    void* Find(const std::string& name, uint32_t minimumVersion = 0) const;
    std::vector<ServiceInfo> List() const;

  private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, ServiceInfo> m_services;
};

} // namespace engine::plugin
