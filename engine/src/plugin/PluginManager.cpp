#include "engine/plugin/PluginManager.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace engine::plugin
{

namespace
{

std::string NativeLibraryExtension()
{
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

void* OpenLibrary(const std::filesystem::path& path, std::string& error)
{
#if defined(_WIN32)
    HMODULE module = LoadLibraryW(path.c_str());
    if (!module)
        error = "LoadLibrary failed with Win32 error " + std::to_string(GetLastError());
    return module;
#else
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module)
    {
        const char* detail = dlerror();
        error = detail ? detail : "dlopen failed";
    }
    return module;
#endif
}

void* FindSymbol(void* module, const char* name)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    return dlsym(module, name);
#endif
}

void CloseLibrary(void* module)
{
    if (!module)
        return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(module));
#else
    dlclose(module);
#endif
}

bool IsCurrentDescriptor(const PluginDescriptor* descriptor)
{
    return descriptor && descriptor->StructSize >= sizeof(PluginDescriptor) &&
           descriptor->AbiVersion == kPluginAbiVersion && descriptor->Name &&
           descriptor->Name[0] != '\0';
}

} // namespace

struct PluginManager::HostContext
{
    PluginManager* Manager = nullptr;
    PluginId Owner = 0;
    const PluginHostApi* Api = nullptr;
};

struct PluginManager::LoadedPlugin
{
    PluginInfo Info;
    const PluginDescriptor* Descriptor = nullptr;
    void* NativeModule = nullptr;
    std::filesystem::path LoadedPath;
    std::unique_ptr<HostContext> Context;
    PluginHostApi HostApi;
};

PluginManager::PluginManager(runtime::ComponentRegistry& components) : m_components(components)
{
    m_searchPaths.push_back(std::filesystem::current_path());
}

PluginManager::~PluginManager()
{
    UnloadAll();
}

void PluginManager::AddSearchPath(std::filesystem::path path)
{
    if (path.empty())
        return;
    std::error_code error;
    path = std::filesystem::weakly_canonical(path, error);
    if (error)
        path = path.lexically_normal();
    if (std::find(m_searchPaths.begin(), m_searchPaths.end(), path) == m_searchPaths.end())
        m_searchPaths.push_back(std::move(path));
}

void PluginManager::BeginChanges()
{
    if (m_changeDepth++ == 0)
        Emit({PluginEventType::BeforePluginChanges});
}

void PluginManager::EndChanges()
{
    if (m_changeDepth == 0)
        return;
    if (--m_changeDepth == 0)
        Emit({PluginEventType::AfterPluginChanges});
}

bool PluginManager::LoadPlugin(const std::filesystem::path& pathOrName, PluginLoadFlags flags)
{
    BeginChanges();
    m_lastError.clear();
    const size_t previousCount = m_loaded.size();
    std::vector<std::string> stack;
    const bool loaded = LoadInternal(pathOrName, flags, stack);
    if (!loaded)
    {
        while (m_loaded.size() > previousCount)
            UnloadAt(m_loaded.size() - 1, false);
    }
    EndChanges();
    return loaded;
}

bool PluginManager::LoadInternal(const std::filesystem::path& pathOrName, PluginLoadFlags flags,
                                 std::vector<std::string>& dependencyStack)
{
    const std::string requested = pathOrName.string();
    if (FindLoaded(requested))
        return true;
    if (const auto staticIt = m_staticPlugins.find(requested); staticIt != m_staticPlugins.end())
        return LoadDescriptor(staticIt->second, {}, nullptr, {}, true, flags, dependencyStack);

    const std::filesystem::path path = ResolvePluginPath(pathOrName);
    if (path.empty())
    {
        if (HasFlag(flags, PluginLoadFlags::Optional))
            return true;
        SetError("Plugin library was not found: " + pathOrName.string(), pathOrName);
        return false;
    }
    return LoadDynamic(path, flags, dependencyStack);
}

bool PluginManager::LoadDynamic(const std::filesystem::path& sourcePath, PluginLoadFlags flags,
                                std::vector<std::string>& dependencyStack)
{
    std::filesystem::path loadedPath = sourcePath;
    if (HasFlag(flags, PluginLoadFlags::LoadCopy))
    {
        loadedPath = sourcePath.parent_path() /
                     (sourcePath.stem().string() + ".engine_hotload_" +
                      std::to_string(++m_hotLoadCounter) + sourcePath.extension().string());
        std::error_code copyError;
        std::filesystem::copy_file(sourcePath, loadedPath,
                                   std::filesystem::copy_options::overwrite_existing, copyError);
        if (copyError)
        {
            SetError("Could not create hot-load copy of plugin '" + sourcePath.string() +
                         "': " + copyError.message(),
                     sourcePath);
            return false;
        }
    }

    std::string openError;
    void* module = OpenLibrary(loadedPath, openError);
    if (!module)
    {
        if (loadedPath != sourcePath)
        {
            std::error_code removeError;
            std::filesystem::remove(loadedPath, removeError);
        }
        SetError("Could not load plugin '" + sourcePath.string() + "': " + openError, sourcePath);
        return false;
    }

    const auto query = reinterpret_cast<QueryPluginFunction>(FindSymbol(module, kPluginEntryPoint));
    if (!query)
    {
        CloseLibrary(module);
        if (loadedPath != sourcePath)
        {
            std::error_code removeError;
            std::filesystem::remove(loadedPath, removeError);
        }
        SetError("Plugin '" + sourcePath.string() + "' does not export " + kPluginEntryPoint,
                 sourcePath);
        return false;
    }

    const PluginDescriptor* descriptor = nullptr;
    try
    {
        descriptor = query(kPluginAbiVersion);
    }
    catch (...)
    {
        CloseLibrary(module);
        if (loadedPath != sourcePath)
        {
            std::error_code removeError;
            std::filesystem::remove(loadedPath, removeError);
        }
        SetError("Plugin '" + sourcePath.string() + "' threw from " + kPluginEntryPoint,
                 sourcePath);
        return false;
    }
    if (!IsCurrentDescriptor(descriptor))
    {
        CloseLibrary(module);
        if (loadedPath != sourcePath)
        {
            std::error_code removeError;
            std::filesystem::remove(loadedPath, removeError);
        }
        SetError("Plugin '" + sourcePath.string() +
                     " rejected engine ABI version " + std::to_string(kPluginAbiVersion),
                 sourcePath);
        return false;
    }

    return LoadDescriptor(descriptor, sourcePath, module, loadedPath, false, flags,
                          dependencyStack);
}

bool PluginManager::LoadDescriptor(const PluginDescriptor* descriptor,
                                   const std::filesystem::path& sourcePath, void* nativeModule,
                                   const std::filesystem::path& loadedPath, bool isStatic,
                                   PluginLoadFlags flags,
                                   std::vector<std::string>& dependencyStack)
{
    const auto closeCandidate = [&]()
    {
        CloseLibrary(nativeModule);
        if (!loadedPath.empty() && loadedPath != sourcePath)
        {
            std::error_code removeError;
            std::filesystem::remove(loadedPath, removeError);
        }
    };

    if (!IsCurrentDescriptor(descriptor))
    {
        closeCandidate();
        SetError("Static plugin has an invalid descriptor or incompatible ABI", sourcePath);
        return false;
    }

    const std::string name = descriptor->Name;
    if (FindLoaded(name))
    {
        closeCandidate();
        return true;
    }
    if (std::find(dependencyStack.begin(), dependencyStack.end(), name) != dependencyStack.end())
    {
        closeCandidate();
        SetError("Plugin dependency cycle detected at '" + name + "'", sourcePath, name);
        return false;
    }

    dependencyStack.push_back(name);
    std::vector<std::string> dependencies;
    dependencies.reserve(descriptor->DependencyCount);
    for (uint32_t index = 0; index < descriptor->DependencyCount; ++index)
    {
        if (!descriptor->Dependencies || !descriptor->Dependencies[index] ||
            descriptor->Dependencies[index][0] == '\0')
        {
            dependencyStack.pop_back();
            closeCandidate();
            SetError("Plugin '" + name + "' has an invalid dependency entry", sourcePath, name);
            return false;
        }
        const std::string dependency = descriptor->Dependencies[index];
        dependencies.push_back(dependency);
        if (FindLoaded(dependency))
            continue;

        if (const auto staticIt = m_staticPlugins.find(dependency);
            staticIt != m_staticPlugins.end())
        {
            if (!LoadDescriptor(staticIt->second, {}, nullptr, {}, true, flags, dependencyStack))
            {
                dependencyStack.pop_back();
                closeCandidate();
                return false;
            }
            continue;
        }

        const std::filesystem::path dependencyPath =
            ResolvePluginPath(dependency, sourcePath.parent_path());
        if (dependencyPath.empty() || !LoadDynamic(dependencyPath, flags, dependencyStack))
        {
            dependencyStack.pop_back();
            closeCandidate();
            if (m_lastError.empty())
                SetError("Dependency '" + dependency + "' required by plugin '" + name +
                             "' was not found",
                         sourcePath, name);
            return false;
        }
    }
    dependencyStack.pop_back();

    auto plugin = std::make_unique<LoadedPlugin>();
    plugin->Info.Id = m_nextPluginId++;
    plugin->Info.Name = name;
    plugin->Info.VersionMajor = descriptor->VersionMajor;
    plugin->Info.VersionMinor = descriptor->VersionMinor;
    plugin->Info.VersionPatch = descriptor->VersionPatch;
    plugin->Info.SourcePath = sourcePath;
    plugin->Info.Dependencies = std::move(dependencies);
    plugin->Info.IsStatic = isStatic;
    plugin->Descriptor = descriptor;
    plugin->NativeModule = nativeModule;
    plugin->LoadedPath = loadedPath;
    plugin->Context = std::make_unique<HostContext>();
    plugin->Context->Manager = this;
    plugin->Context->Owner = plugin->Info.Id;
    plugin->HostApi.HostContext = plugin->Context.get();
    plugin->HostApi.Log = &PluginManager::HostLog;
    plugin->HostApi.RegisterService = &PluginManager::HostRegisterService;
    plugin->HostApi.UnregisterService = &PluginManager::HostUnregisterService;
    plugin->HostApi.GetService = &PluginManager::HostGetService;
    plugin->HostApi.RegisterComponentType = &PluginManager::HostRegisterComponentType;
    plugin->HostApi.UnregisterComponentType = &PluginManager::HostUnregisterComponentType;
    plugin->Context->Api = &plugin->HostApi;

    Emit({PluginEventType::BeforeLoading, name, sourcePath});
    LoadedPlugin* current = plugin.get();
    m_loaded.push_back(std::move(plugin));
    Emit({PluginEventType::AfterLoadingBeforeInit, name, sourcePath});

    int32_t loadResult = 0;
    try
    {
        if (descriptor->OnLoad)
            loadResult = descriptor->OnLoad(&current->HostApi);
    }
    catch (...)
    {
        loadResult = -1;
    }
    if (loadResult != 0)
    {
        std::string cleanupError;
        m_components.UnregisterOwner(current->Info.Id, &cleanupError);
        m_services.UnregisterOwner(current->Info.Id);
        m_loaded.pop_back();
        closeCandidate();
        SetError("Plugin '" + name + "' failed during OnLoad", sourcePath, name);
        return false;
    }

    Emit({PluginEventType::AfterLoading, name, sourcePath});
    log::Info("Loaded runtime plugin: " + name);
    return true;
}

bool PluginManager::UnloadPlugin(const std::string& name)
{
    BeginChanges();
    m_lastError.clear();
    const auto it = std::find_if(m_loaded.begin(), m_loaded.end(),
                                 [&](const auto& plugin) { return plugin->Info.Name == name; });
    if (it == m_loaded.end())
    {
        SetError("Runtime plugin is not loaded: " + name, {}, name);
        EndChanges();
        return false;
    }
    const bool result = UnloadAt(static_cast<size_t>(std::distance(m_loaded.begin(), it)), true);
    EndChanges();
    return result;
}

bool PluginManager::UnloadAt(size_t index, bool checkDependents)
{
    if (index >= m_loaded.size())
        return false;
    LoadedPlugin& plugin = *m_loaded[index];
    if (checkDependents)
    {
        for (const auto& candidate : m_loaded)
        {
            if (candidate.get() == &plugin)
                continue;
            if (std::find(candidate->Info.Dependencies.begin(), candidate->Info.Dependencies.end(),
                          plugin.Info.Name) != candidate->Info.Dependencies.end())
            {
                SetError("Cannot unload plugin '" + plugin.Info.Name + "' while plugin '" +
                             candidate->Info.Name + "' depends on it",
                         plugin.Info.SourcePath, plugin.Info.Name);
                return false;
            }
        }
    }

    const size_t liveComponents = m_components.LiveInstancesForOwner(plugin.Info.Id);
    if (liveComponents != 0)
    {
        SetError("Cannot unload plugin '" + plugin.Info.Name + "' while " +
                     std::to_string(liveComponents) + " component instance(s) are alive",
                 plugin.Info.SourcePath, plugin.Info.Name);
        return false;
    }

    const PluginEvent event{PluginEventType::BeforeUnloading, plugin.Info.Name,
                            plugin.Info.SourcePath};
    Emit(event);
    if (plugin.Descriptor->OnUnload)
    {
        try
        {
            plugin.Descriptor->OnUnload(&plugin.HostApi);
        }
        catch (...)
        {
            log::Error("Runtime plugin threw during OnUnload: " + plugin.Info.Name);
        }
    }

    std::string registryError;
    if (!m_components.UnregisterOwner(plugin.Info.Id, &registryError))
    {
        SetError(registryError, plugin.Info.SourcePath, plugin.Info.Name);
        return false;
    }
    m_services.UnregisterOwner(plugin.Info.Id);

    const std::string name = plugin.Info.Name;
    const std::filesystem::path source = plugin.Info.SourcePath;
    const std::filesystem::path loadedPath = plugin.LoadedPath;
    const bool copied = !loadedPath.empty() && loadedPath != source;
    CloseLibrary(plugin.NativeModule);
    m_loaded.erase(m_loaded.begin() + static_cast<std::ptrdiff_t>(index));
    if (copied)
    {
        std::error_code removeError;
        std::filesystem::remove(loadedPath, removeError);
    }
    Emit({PluginEventType::AfterUnloading, name, source});
    log::Info("Unloaded runtime plugin: " + name);
    return true;
}

bool PluginManager::UnloadAll()
{
    BeginChanges();
    bool success = true;
    for (size_t index = m_loaded.size(); index > 0;)
    {
        --index;
        if (!UnloadAt(index, false))
            success = false;
    }
    EndChanges();
    return success;
}

bool PluginManager::RegisterStaticPlugin(const PluginDescriptor* descriptor)
{
    if (!IsCurrentDescriptor(descriptor))
    {
        SetError("Cannot register a static plugin with an invalid descriptor or ABI");
        return false;
    }
    if (m_staticPlugins.contains(descriptor->Name) || FindLoaded(descriptor->Name))
    {
        SetError("Static plugin is already registered: " + std::string(descriptor->Name), {},
                 descriptor->Name);
        return false;
    }
    m_staticPlugins.emplace(descriptor->Name, descriptor);
    return true;
}

bool PluginManager::LoadStaticPlugins()
{
    bool success = true;
    for (const auto& [name, descriptor] : m_staticPlugins)
        success = LoadPlugin(name) && success;
    return success;
}

void PluginManager::BroadcastApplicationEvent(const PluginApplicationEvent& event)
{
    for (const auto& plugin : m_loaded)
        if (plugin->Descriptor->OnApplicationEvent)
        {
            try
            {
                plugin->Descriptor->OnApplicationEvent(&plugin->HostApi, &event);
            }
            catch (...)
            {
                log::Error("Runtime plugin threw while handling an application event: " +
                           plugin->Info.Name);
            }
        }
}

uint64_t PluginManager::AddEventListener(EventCallback callback)
{
    const uint64_t id = m_nextListenerId++;
    m_listeners.emplace_back(id, std::move(callback));
    return id;
}

void PluginManager::RemoveEventListener(uint64_t listenerId)
{
    std::erase_if(m_listeners, [&](const auto& item) { return item.first == listenerId; });
}

bool PluginManager::IsLoaded(const std::string& name) const
{
    return FindLoaded(name) != nullptr;
}

std::vector<PluginInfo> PluginManager::LoadedPlugins() const
{
    std::vector<PluginInfo> result;
    result.reserve(m_loaded.size());
    for (const auto& plugin : m_loaded)
        result.push_back(plugin->Info);
    return result;
}

std::filesystem::path PluginManager::ResolvePluginPath(
    const std::filesystem::path& pathOrName, const std::filesystem::path& relativeTo) const
{
    if (pathOrName.empty())
        return {};
    std::vector<std::filesystem::path> candidates;
    const bool hasExtension = pathOrName.has_extension();
    auto appendCandidate = [&](const std::filesystem::path& base)
    {
        candidates.push_back(base / pathOrName);
        if (!hasExtension)
        {
            candidates.push_back(base / (pathOrName.string() + NativeLibraryExtension()));
#if !defined(_WIN32)
            candidates.push_back(base / ("lib" + pathOrName.string() + NativeLibraryExtension()));
#endif
        }
    };

    if (pathOrName.is_absolute())
    {
        candidates.push_back(pathOrName);
        if (!hasExtension)
            candidates.push_back(pathOrName.string() + NativeLibraryExtension());
    }
    else
    {
        if (!relativeTo.empty())
            appendCandidate(relativeTo);
        for (const auto& searchPath : m_searchPaths)
            appendCandidate(searchPath);
    }

    for (const auto& candidate : candidates)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
            return std::filesystem::weakly_canonical(candidate, error);
    }
    return {};
}

void PluginManager::Emit(PluginEvent event) const
{
    const auto listeners = m_listeners;
    for (const auto& [id, callback] : listeners)
        if (callback)
            callback(event);
}

void PluginManager::SetError(std::string error, const std::filesystem::path& path,
                             const std::string& name)
{
    m_lastError = std::move(error);
    log::Error(m_lastError);
    Emit({PluginEventType::LoadFailed, name, path, m_lastError});
}

PluginManager::LoadedPlugin* PluginManager::FindLoaded(const std::string& name)
{
    const auto it = std::find_if(m_loaded.begin(), m_loaded.end(),
                                 [&](const auto& plugin) { return plugin->Info.Name == name; });
    return it == m_loaded.end() ? nullptr : it->get();
}

const PluginManager::LoadedPlugin* PluginManager::FindLoaded(const std::string& name) const
{
    const auto it = std::find_if(m_loaded.begin(), m_loaded.end(),
                                 [&](const auto& plugin) { return plugin->Info.Name == name; });
    return it == m_loaded.end() ? nullptr : it->get();
}

void PluginManager::HostLog(void* context, PluginLogLevel level, const char* message)
{
    const HostContext* host = static_cast<const HostContext*>(context);
    const std::string text = "[plugin " + std::to_string(host ? host->Owner : 0) + "] " +
                             (message ? message : "");
    switch (level)
    {
    case PluginLogLevel::Warning:
        log::Warn(text);
        break;
    case PluginLogLevel::Error:
        log::Error(text);
        break;
    default:
        log::Info(text);
        break;
    }
}

int32_t PluginManager::HostRegisterService(void* context, const char* name, uint32_t version,
                                           void* service)
{
    HostContext* host = static_cast<HostContext*>(context);
    if (!host || !host->Manager || !name)
        return -1;
    std::string error;
    if (!host->Manager->m_services.Register(host->Owner, name, version, service, &error))
    {
        host->Manager->SetError(error, {}, std::to_string(host->Owner));
        return -1;
    }
    return 0;
}

int32_t PluginManager::HostUnregisterService(void* context, const char* name)
{
    HostContext* host = static_cast<HostContext*>(context);
    if (!host || !host->Manager || !name)
        return -1;
    std::string error;
    return host->Manager->m_services.Unregister(host->Owner, name, &error) ? 0 : -1;
}

void* PluginManager::HostGetService(void* context, const char* name, uint32_t minimumVersion)
{
    HostContext* host = static_cast<HostContext*>(context);
    if (!host || !host->Manager || !name)
        return nullptr;
    return host->Manager->m_services.Find(name, minimumVersion);
}

int32_t PluginManager::HostRegisterComponentType(void* context,
                                                 const PluginComponentType* componentType)
{
    HostContext* host = static_cast<HostContext*>(context);
    if (!host || !host->Manager || !componentType)
        return -1;
    std::string error;
    if (!host->Manager->m_components.Register(host->Owner, *componentType, host->Api, &error))
    {
        host->Manager->SetError(error, {}, std::to_string(host->Owner));
        return -1;
    }
    return 0;
}

int32_t PluginManager::HostUnregisterComponentType(void* context, const char* typeName)
{
    HostContext* host = static_cast<HostContext*>(context);
    if (!host || !host->Manager || !typeName)
        return -1;
    std::string error;
    return host->Manager->m_components.Unregister(host->Owner, typeName, &error) ? 0 : -1;
}

} // namespace engine::plugin
