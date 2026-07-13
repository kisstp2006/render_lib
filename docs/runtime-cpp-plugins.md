# Runtime C++ plugins and the behavior world

Reference revision: `ezEngine/ezEngine@2fe8e9b1f5605862f67d7bc1e6c8ee780b8b3ef1`.

Primary references:

- [ezEngine runtime plugin API](https://github.com/ezEngine/ezEngine/blob/dev/Code/Engine/Foundation/Configuration/Plugin.h)
- [ezEngine world / scenegraph overview](https://ezengine.net/pages/docs/runtime/world/world-overview.html)
- [ezEngine game objects](https://ezengine.net/pages/docs/runtime/world/game-objects.html)
- [ezEngine components](https://ezengine.net/pages/docs/runtime/world/components.html)
- [ezEngine RmlUi runtime startup](https://github.com/ezEngine/ezEngine/blob/dev/Code/EnginePlugins/RmlUiPlugin/Startup.cpp)
- [ezEngine Dear ImGui integration](https://github.com/ezEngine/ezEngine/blob/dev/Code/Engine/GameEngine/DearImgui/Implementation/DearImgui.cpp)

## What was taken from ezEngine

The relevant part is the runtime module system, not editor extensions. ezEngine
loads native libraries, resolves declared dependencies before initialization,
broadcasts grouped load/unload events, supports loading a copied DLL during
development and shuts modules down in reverse order. More complex integrations
use dependency-ordered subsystems for symmetrical startup and shutdown.

Dear ImGui illustrates an application service: it owns contexts and texture
registrations, subscribes to game-application execution events, and removes
the handler and resources during shutdown. RmlUi illustrates a larger runtime
plugin: its subsystem depends on Foundation, Core and the render graph, then
registers its resource loader/fallback and singleton; shutdown reverses those
registrations. Its 2D and 3D canvases are ordinary runtime components.

This engine keeps those architectural rules, but does not copy ezEngine's
reflection macros, editor property system or global static-registration
mechanism. The current renderer is a static library, so exposing STL ownership
or arbitrary virtual interfaces across a DLL boundary would be fragile. Native
plugins therefore use a versioned POD function table. Plugin implementation
code is still normal C++.

## Implemented runtime plugin API

`PluginManager` provides:

- Windows DLL and POSIX shared-library loading;
- a single `EngineQueryPlugin` entry point with ABI and descriptor-size checks;
- named, transitive dependencies with cycle detection and provider-first init;
- reverse-order bulk shutdown and dependent-plugin unload protection;
- optional and copy-on-load flags;
- static-plugin registration for monolithic/platform builds;
- before/after grouped changes, load/init/unload and failure events;
- application lifecycle events for frame, update, render and focus changes;
- versioned, named services for optional systems such as UI, physics or audio;
- ABI v2 host-owned tagged allocation/free functions for measurable DLL memory;
- automatic cleanup of registrations owned by an unloading plugin;
- unload blocking while any component still owns callbacks in that module;
- useful `LastError()` diagnostics.

The public ABI is in `engine/plugin/PluginApi.h`. The host owns registries and
module handles. A plugin owns and destroys every object it allocates. Service
pointers are opaque; plugins must declare a dependency on the provider before
retaining one beyond the call that queried it.

Plugin components created through `MakeComponentType<T>()` use the host
allocator automatically. Other plugin allocations that should appear in the
engine memory report must use `host->Allocate` and `host->Free`; allocator
families must never be mixed across the DLL boundary.

## Minimal C++ plugin

```cpp
#include <engine/plugin/PluginApi.h>

namespace {
int g_service = 42;

int32_t OnLoad(const engine::plugin::PluginHostApi* host)
{
    return host->RegisterService(host->HostContext, "example.counter", 1,
                                 &g_service);
}

void OnUnload(const engine::plugin::PluginHostApi* host)
{
    host->UnregisterService(host->HostContext, "example.counter");
}

const engine::plugin::PluginDescriptor descriptor = {
    sizeof(engine::plugin::PluginDescriptor),
    engine::plugin::kPluginAbiVersion,
    "ExamplePlugin",
    1, 0, 0,
    nullptr, 0,
    &OnLoad, &OnUnload, nullptr
};
}

ENGINE_PLUGIN_ENTRY
{
    return hostAbiVersion == engine::plugin::kPluginAbiVersion
        ? &descriptor : nullptr;
}
```

The module must define `ENGINE_PLUGIN_BUILD=1`, include the engine public
headers and export no other engine-facing symbol. It intentionally does not
link the static `engine` target:

```cmake
add_library(ExamplePlugin MODULE ExamplePlugin.cpp)
target_include_directories(ExamplePlugin PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
target_compile_definitions(ExamplePlugin PRIVATE ENGINE_PLUGIN_BUILD=1)
set_target_properties(ExamplePlugin PROPERTIES PREFIX "")
```

Load it from an application:

```cpp
engine::Application app(config);
app.GetPlugins().AddSearchPath("plugins");
if (!app.GetPlugins().LoadPlugin("ExamplePlugin",
        engine::plugin::PluginLoadFlags::LoadCopy))
    throw std::runtime_error(app.GetPlugins().LastError());
```

## Plugin components and entities

`runtime::World` is a behavior layer beside the renderer-neutral `Scene`; it
does not create a second OpenGL/Vulkan scene. `Scene` remains the common CPU
render data consumed by both backends. `World` adds:

- generation-checked entity handles instead of persistent raw pointers;
- name, tag and 32-bit layer;
- parent-child DAG with cycle rejection;
- local position/rotation/scale and derived world transforms;
- active-state propagation through a hierarchy;
- recursive entity destruction, deferred safely when called during update;
- plugin-registerable component types;
- create/destroy, activate/deactivate, start and per-frame update lifecycle;
- per-component enable state.

A plugin component can use the header-only C++ adapter:

```cpp
class Rotator final : public engine::runtime::Component
{
public:
    using Component::Component;
    void OnUpdate(float dt) override { elapsed += dt; }
    float elapsed = 0.0f;
};

auto rotatorType =
    engine::runtime::MakeComponentType<Rotator>("game.Rotator");

// OnLoad:
host->RegisterComponentType(host->HostContext, &rotatorType);
```

The application owns one world and updates it between `BeforeUpdate` and
`AfterUpdate`:

```cpp
auto entity = app.GetWorld().CreateEntity("Bottle");
app.GetWorld().SetTag(entity, "Product");
app.GetWorld().AddComponent(entity, "game.Rotator");
```

Destroy all entities containing a plugin's components before unloading that
plugin. The manager checks this rule and refuses an unsafe unload.

## UI integration boundary

No ImGui or RmlUi dependency was added by this work. The infrastructure they
need is now present: runtime modules, dependency ordering, services,
application events and components. A future UI plugin should own the UI
library contexts and allocations, register a versioned UI service, subscribe
through `OnApplicationEvent`, expose 2D/3D canvas components where useful, and
remove render resources and registrations before its DLL unloads. Actual GPU
drawing should be added through a backend-neutral render extension/render
graph interface, rather than reaching directly into OpenGL or Vulkan.

## Automated coverage

`TestRuntimePlugin.dll` and `TestDependentPlugin.dll` are real C++ test
modules. The tests cover copied DLL loading, service/component registration,
application events, hierarchy transforms and activation, component lifecycle,
recursive destruction, blocked unsafe unload, automatic dependency loading,
provider protection and reverse bulk shutdown.
