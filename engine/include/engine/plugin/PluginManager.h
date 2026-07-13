#pragma once

#include "engine/plugin/PluginApi.h"
#include "engine/plugin/ServiceRegistry.h"
#include "engine/runtime/ComponentRegistry.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::plugin
{

enum class PluginLoadFlags : uint32_t
{
    None = 0,
    Optional = 1u << 0u,
    LoadCopy = 1u << 1u
};

constexpr PluginLoadFlags operator|(PluginLoadFlags left, PluginLoadFlags right)
{
    return static_cast<PluginLoadFlags>(static_cast<uint32_t>(left) |
                                        static_cast<uint32_t>(right));
}

constexpr bool HasFlag(PluginLoadFlags value, PluginLoadFlags flag)
{
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

enum class PluginEventType
{
    BeforePluginChanges,
    BeforeLoading,
    AfterLoadingBeforeInit,
    AfterLoading,
    BeforeUnloading,
    AfterUnloading,
    AfterPluginChanges,
    LoadFailed
};

struct PluginEvent
{
    PluginEventType Type = PluginEventType::BeforeLoading;
    std::string Name;
    std::filesystem::path Path;
    std::string Message;
};

struct PluginInfo
{
    PluginId Id = 0;
    std::string Name;
    uint32_t VersionMajor = 0;
    uint32_t VersionMinor = 0;
    uint32_t VersionPatch = 0;
    std::filesystem::path SourcePath;
    std::vector<std::string> Dependencies;
    bool IsStatic = false;
};

class PluginManager
{
  public:
    using EventCallback = std::function<void(const PluginEvent&)>;

    explicit PluginManager(runtime::ComponentRegistry& components);
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    void AddSearchPath(std::filesystem::path path);
    bool LoadPlugin(const std::filesystem::path& pathOrName,
                    PluginLoadFlags flags = PluginLoadFlags::None);
    bool UnloadPlugin(const std::string& name);
    bool UnloadAll();

    // Static registration uses the exact same descriptor and lifecycle. It is
    // useful for platforms that disallow dynamic libraries and for monolithic
    // shipping builds.
    bool RegisterStaticPlugin(const PluginDescriptor* descriptor);
    bool LoadStaticPlugins();

    void BroadcastApplicationEvent(const PluginApplicationEvent& event);

    uint64_t AddEventListener(EventCallback callback);
    void RemoveEventListener(uint64_t listenerId);

    bool IsLoaded(const std::string& name) const;
    std::vector<PluginInfo> LoadedPlugins() const;
    const std::string& LastError() const { return m_lastError; }

    ServiceRegistry& Services() { return m_services; }
    const ServiceRegistry& Services() const { return m_services; }
    runtime::ComponentRegistry& Components() { return m_components; }

  private:
    struct HostContext;
    struct LoadedPlugin;

    bool LoadInternal(const std::filesystem::path& pathOrName, PluginLoadFlags flags,
                      std::vector<std::string>& dependencyStack);
    bool LoadDynamic(const std::filesystem::path& sourcePath, PluginLoadFlags flags,
                     std::vector<std::string>& dependencyStack);
    bool LoadDescriptor(const PluginDescriptor* descriptor, const std::filesystem::path& sourcePath,
                        void* nativeModule, const std::filesystem::path& loadedPath,
                        bool isStatic, PluginLoadFlags flags,
                        std::vector<std::string>& dependencyStack);
    bool UnloadAt(size_t index, bool checkDependents);
    std::filesystem::path ResolvePluginPath(const std::filesystem::path& pathOrName,
                                            const std::filesystem::path& relativeTo = {}) const;
    void BeginChanges();
    void EndChanges();
    void Emit(PluginEvent event) const;
    void SetError(std::string error, const std::filesystem::path& path = {},
                  const std::string& name = {});
    LoadedPlugin* FindLoaded(const std::string& name);
    const LoadedPlugin* FindLoaded(const std::string& name) const;

    static void HostLog(void* context, PluginLogLevel level, const char* message);
    static int32_t HostRegisterService(void* context, const char* name, uint32_t version,
                                       void* service);
    static int32_t HostUnregisterService(void* context, const char* name);
    static void* HostGetService(void* context, const char* name, uint32_t minimumVersion);
    static int32_t HostRegisterComponentType(void* context,
                                             const PluginComponentType* componentType);
    static int32_t HostUnregisterComponentType(void* context, const char* typeName);

    runtime::ComponentRegistry& m_components;
    ServiceRegistry m_services;
    std::vector<std::unique_ptr<LoadedPlugin>> m_loaded;
    std::unordered_map<std::string, const PluginDescriptor*> m_staticPlugins;
    std::vector<std::filesystem::path> m_searchPaths;
    std::vector<std::pair<uint64_t, EventCallback>> m_listeners;
    std::string m_lastError;
    PluginId m_nextPluginId = 1;
    uint64_t m_nextListenerId = 1;
    uint32_t m_changeDepth = 0;
    uint64_t m_hotLoadCounter = 0;
};

} // namespace engine::plugin
