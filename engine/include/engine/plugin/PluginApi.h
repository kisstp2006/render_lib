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

inline constexpr uint32_t kPluginAbiVersion = 3;
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

enum class PluginPropertyType : uint32_t
{
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    Vector2,
    Vector3,
    Vector4,
    Quaternion,
    String,
    AssetGuid,
    Enumeration
};

enum PluginPropertyFlags : uint32_t
{
    PluginPropertyNone = 0,
    PluginPropertyReadOnly = 1u << 0u,
    PluginPropertyColor = 1u << 1u,
    PluginPropertyAngleDegrees = 1u << 2u,
    PluginPropertyMultiline = 1u << 3u,
    PluginPropertyHidden = 1u << 4u,
    PluginPropertyTransient = 1u << 5u,
    PluginPropertyHasRange = 1u << 6u
};

// Plugin properties use a type-tagged textual value at the ABI boundary.
// Query with output=nullptr to obtain the required byte count (including the
// terminator), then call again with storage. Returning zero indicates success.
struct PluginComponentProperty
{
    uint32_t StructSize = sizeof(PluginComponentProperty);
    const char* Name = nullptr;
    const char* DisplayName = nullptr;
    PluginPropertyType Type = PluginPropertyType::String;
    uint32_t Flags = PluginPropertyNone;
    double Minimum = 0.0;
    double Maximum = 0.0;
    double Step = 0.0;
    const char* EnumValues = nullptr; // Semicolon-separated labels.
    int32_t (*GetText)(const void* instance, char* output, size_t* inOutBytes) = nullptr;
    int32_t (*SetText)(void* instance, const char* value, size_t bytes) = nullptr;
};

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
    const PluginComponentProperty* Properties = nullptr;
    uint32_t PropertyCount = 0;
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
    void* (*Allocate)(void* hostContext, size_t size, size_t alignment, const char* tag) = nullptr;
    void (*Free)(void* hostContext, void* memory) = nullptr;
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
