#pragma once

#include "engine/plugin/PluginApi.h"

#include <new>
#include <type_traits>

namespace engine::runtime
{

// Optional ergonomic C++ base for plugin components. Instances are always
// created and destroyed by callbacks compiled into the owning plugin. Storage
// comes from the host allocator so ownership and memory profiling remain on the
// engine side of the DLL boundary.
class Component
{
  public:
    Component(plugin::EntityId owner, const plugin::PluginHostApi& host)
        : m_owner(owner), m_host(host)
    {
    }
    virtual ~Component() = default;

    plugin::EntityId Owner() const { return m_owner; }
    const plugin::PluginHostApi& PluginHost() const { return m_host; }

    virtual void OnStart() {}
    virtual void OnActivate() {}
    virtual void OnDeactivate() {}
    virtual void OnUpdate(float) {}

  protected:
    const plugin::PluginHostApi& Host() const { return m_host; }

  private:
    plugin::EntityId m_owner = 0;
    const plugin::PluginHostApi& m_host;
};

// T must derive from Component and provide:
//   T(EntityId owner, const PluginHostApi& host)
// The returned descriptor should normally live in static plugin storage.
template <typename T> plugin::PluginComponentType MakeComponentType(const char* typeName)
{
    static_assert(std::is_base_of_v<Component, T>);
    plugin::PluginComponentType type;
    type.TypeName = typeName;
    type.Create = [](plugin::EntityId owner, const plugin::PluginHostApi* host) -> void*
    {
        if (!host)
            return nullptr;
        try
        {
            if (!host->Allocate || !host->Free)
                return nullptr;
            void* storage = host->Allocate(host->HostContext, sizeof(T), alignof(T), nullptr);
            if (!storage)
                return nullptr;
            try
            {
                return ::new (storage) T(owner, *host);
            }
            catch (...)
            {
                host->Free(host->HostContext, storage);
                throw;
            }
        }
        catch (...)
        {
            if (host->Log)
                host->Log(host->HostContext, plugin::PluginLogLevel::Error,
                          "Plugin component construction threw an exception");
            return nullptr;
        }
    };
    type.Destroy = [](void* instance)
    {
        try
        {
            T* component = static_cast<T*>(instance);
            const plugin::PluginHostApi& host = component->PluginHost();
            component->~T();
            if (host.Free)
                host.Free(host.HostContext, component);
        }
        catch (...)
        {
        }
    };
    type.OnStart = [](void* instance)
    {
        try
        {
            static_cast<T*>(instance)->OnStart();
        }
        catch (...)
        {
        }
    };
    type.OnActivate = [](void* instance)
    {
        try
        {
            static_cast<T*>(instance)->OnActivate();
        }
        catch (...)
        {
        }
    };
    type.OnDeactivate = [](void* instance)
    {
        try
        {
            static_cast<T*>(instance)->OnDeactivate();
        }
        catch (...)
        {
        }
    };
    type.OnUpdate = [](void* instance, float deltaSeconds)
    {
        try
        {
            static_cast<T*>(instance)->OnUpdate(deltaSeconds);
        }
        catch (...)
        {
        }
    };
    return type;
}

} // namespace engine::runtime
