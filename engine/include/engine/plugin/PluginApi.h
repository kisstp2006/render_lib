#pragma once

#include <cstddef>
#include <cstdint>

// Runtime plugins are implemented in C++, but deliberately cross the module
// boundary through this versioned POD API. This avoids sharing STL ownership,
// allocators or renderer-backend objects between the executable and a DLL/SO.

#if defined(_WIN32) && defined(ENGINE_PLUGIN_BUILD)
#define ENGINE_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) && defined(ENGINE_PLUGIN_BUILD)
#define ENGINE_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define ENGINE_PLUGIN_EXPORT
#endif

namespace engine::plugin
{

inline constexpr uint32_t kPluginAbiVersion = 1;
inline constexpr const char* kPluginEntryPoint = "EngineQueryPlugin";

using PluginId = uint64_t;
using EntityId = uint64_t;

enum class PluginLogLevel : uint32_t
{
    Info,
    Warning,
    Error
};

enum class PluginApplicationEventType : uint32_t
{
    BeginFrame,
    EnteredBackground,
    EnteredForeground,
    BeforeUpdate,
    AfterUpdate,
    BeforeRender,
    AfterRender,
    EndFrame
};

struct PluginApplicationEvent
{
    PluginApplicationEventType Type = PluginApplicationEventType::BeginFrame;
    float DeltaSeconds = 0.0f;
    uint64_t FrameIndex = 0;
};

struct PluginHostApi;

struct PluginComponentType
{
    uint32_t StructSize = sizeof(PluginComponentType);
    uint32_t Version = 1;
    const char* TypeName = nullptr;

    void* (*Create)(EntityId owner, const PluginHostApi* host) = nullptr;
    void (*Destroy)(void* instance) = nullptr;
    void (*OnStart)(void* instance) = nullptr;
    void (*OnActivate)(void* instance) = nullptr;
    void (*OnDeactivate)(void* instance) = nullptr;
    void (*OnUpdate)(void* instance, float deltaSeconds) = nullptr;
};

struct PluginHostApi
{
    uint32_t StructSize = sizeof(PluginHostApi);
    uint32_t AbiVersion = kPluginAbiVersion;
    void* HostContext = nullptr;

    void (*Log)(void* hostContext, PluginLogLevel level, const char* message) = nullptr;
    int32_t (*RegisterService)(void* hostContext, const char* name, uint32_t version,
                               void* service) = nullptr;
    int32_t (*UnregisterService)(void* hostContext, const char* name) = nullptr;
    void* (*GetService)(void* hostContext, const char* name, uint32_t minimumVersion) = nullptr;
    int32_t (*RegisterComponentType)(void* hostContext,
                                     const PluginComponentType* componentType) = nullptr;
    int32_t (*UnregisterComponentType)(void* hostContext, const char* typeName) = nullptr;
};

struct PluginDescriptor
{
    uint32_t StructSize = sizeof(PluginDescriptor);
    uint32_t AbiVersion = kPluginAbiVersion;
    const char* Name = nullptr;
    uint32_t VersionMajor = 0;
    uint32_t VersionMinor = 1;
    uint32_t VersionPatch = 0;

    const char* const* Dependencies = nullptr;
    uint32_t DependencyCount = 0;

    // Return zero on success. The manager automatically removes registrations
    // if startup fails or a plugin forgets to unregister them during shutdown.
    int32_t (*OnLoad)(const PluginHostApi* host) = nullptr;
    void (*OnUnload)(const PluginHostApi* host) = nullptr;
    void (*OnApplicationEvent)(const PluginHostApi* host,
                               const PluginApplicationEvent* event) = nullptr;
};

using QueryPluginFunction = const PluginDescriptor* (*)(uint32_t hostAbiVersion);

} // namespace engine::plugin

#define ENGINE_PLUGIN_ENTRY                                                                        \
    extern "C" ENGINE_PLUGIN_EXPORT const ::engine::plugin::PluginDescriptor* EngineQueryPlugin( \
        uint32_t hostAbiVersion)
