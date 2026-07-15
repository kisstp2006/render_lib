# Editor runtime foundation

This layer supplies editor-facing engine services without implementing an
editor application. OpenGL and Vulkan expose the same APIs; `Scene`, `World`,
components and serialized files contain no native graphics types.

## Offscreen scene viewports

`RenderViewportHandle` owns one persistent view and its independent HDR,
depth, velocity, bloom, TAA, exposure and Hi-Z history. Any number of handles
may coexist. Resize replaces native targets atomically and increments the
returned texture generation; an allocation failure leaves the old target
valid.

```cpp
auto& backend = app.GetBackend();
auto& renderer = app.GetSceneRenderer();
RenderViewportHandle viewport = renderer.CreateViewport(
    backend, {1280, 720, "Perspective"});

renderer.ResizeViewport(backend, viewport, width, height);
renderer.RenderViewport(backend, viewport, scene, camera,
                        width, height, timeSeconds, deltaSeconds);
RenderTextureHandle image = backend.GetViewportTexture(viewport);

renderer.DestroyViewport(backend, viewport);
```

`RenderTextureHandle` is a non-owning, generation-tagged UI token. OpenGL
provides a texture name; Vulkan provides an image view and sampler in
`SHADER_READ_ONLY_OPTIMAL`. Never cache a native binding across a generation
change.

## Dear ImGui docking UI

The build uses the official docking branch and can be disabled completely with
`-DENGINE_ENABLE_IMGUI=OFF`. Runtime applications remain UI-free unless
`ApplicationDesc::EnableImGui` is true. Native OS platform windows are
independently controlled by `EnableImGuiPlatformViewports`.
Docking layout is stored in `.cache/editor/imgui.ini` by default; set
`ApplicationDesc::ImGuiIniFilename` to another path or an empty string to
disable persistence.

```cpp
ApplicationDesc desc;
desc.EnableImGui = true;
desc.EnableImGuiPlatformViewports = true; // optional
Application app(desc);

app.SetImGuiCallback([&] {
    ImGui::DockSpaceOverViewport();
    ImGui::Begin("Scene");
    ImVec2 size = ImGui::GetContentRegionAvail();
    // Resize/render the engine viewport here, then display its latest token.
    app.GetImGuiLayer()->DrawViewportImage(
        app.GetBackend().GetViewportTexture(viewport), size);
    ImGui::End();
});
```

The layer installs GLFW plus OpenGL/Vulkan backend hooks, draws after the
engine's final backbuffer pass, recreates its native state during renderer hot
reload and automatically forwards `WantCaptureKeyboard` / `WantCaptureMouse`
to `Input`. Raw polling remains available through `IsKeyDownRaw` and
`IsMouseButtonDownRaw` for deliberate global shortcuts.

Vulkan descriptor bindings are cached by viewport and generation. Call
`ImGuiLayer::ForgetTexture(viewport)` before destroying a viewport that was
shown by ImGui.

## World to render Scene bridge

Set `ApplicationDesc::SynchronizeWorldToScene` for World-owned scenes. The
application registers and synchronizes these reflected components each frame:

- `Engine.MeshRenderer`
- `Engine.PointLight`, `Engine.SpotLight`, `Engine.AreaLight`
- `Engine.DirectionalLight`
- `Engine.Camera`
- `Engine.Environment`

Hierarchy transforms, active state, component enable state, materials,
cookies, camera projection and environment settings reach the flat render
`Scene` before the same frame is rendered. Asset GUID resolvers can be supplied
through `WorldRenderBridge::SetResolvers`. Removing or disabling a render
component removes its render data on the next synchronization.

## Reflection and scene files

`ComponentRegistry::List()` and `Properties(typeName)` expose typed metadata:
stable/display names, scalar/vector/quaternion/string/GUID/enum type, color and
angle hints, optional numeric range/step, flags and type-safe get/set
callbacks. `World::GetComponentProperty` and `SetComponentProperty` are the
normal inspector entry points. Native and C++ plugin components use the same
metadata model.

Every entity owns a stable `AssetGuid`, independent of its generation-checked
runtime `EntityId`. The complete hierarchy, transform, activation, component
enable/version, reflected values and active camera round-trip through the
existing versioned scene asset format:

```cpp
std::string error;
assets::SaveWorldScene("scenes/product.sla-scene", app.GetWorld(),
                       activeCameraEntity, &error);
assets::LoadWorldScene("scenes/product.sla-scene", app.GetWorld(),
                       &activeCameraEntity, &error);
```

Properties marked `Transient` are omitted. Hidden properties are still
serialized because `Hidden` is only an inspector presentation hint. Invalid
GUIDs, missing parents, unknown components/properties and malformed values
produce entity/component/property-qualified errors.

## Picking, selection and debug draw

`PickScene` constructs the correct perspective or orthographic camera ray and
returns the nearest World entity whose transformed mesh AABB is hit.
`ApplyPickingSelection` updates `Scene::SelectedEntity`; the shared renderer
then adds the same orange selection AABB highlight on OpenGL and Vulkan.

```cpp
PickingResult hit = PickScene(scene, camera, mouseX, mouseY, width, height);
ApplyPickingSelection(scene, hit);

scene.DebugDraw().Grid(20.0f, 1.0f);
scene.DebugDraw().Line(a, b, {1, 0, 0}, DebugDepthMode::DepthTested);
scene.DebugDraw().Aabb(minimum, maximum, {0, 1, 0});
scene.DebugDraw().Icon(DebugIconType::PointLight, position, {1, .8f, .4f});
```

Lines, AABBs, grid and camera/light billboard glyphs support depth-tested and
overlay modes. Icons are resolved against each viewport camera, so they remain
screen-facing in every view.

## Editor console sink

```cpp
log::SinkId sink = log::AddSink([](const log::Record& record) {
    // record: severity, category, message, timestamp, thread and sequence
    editorConsole.Enqueue(record);
});

// Before destroying the receiving panel/queue:
log::RemoveSink(sink);
```

Registration and dispatch are thread-safe, callbacks run without holding the
logger mutex, exceptions are contained, and recursive logging cannot recurse
back into sinks indefinitely.

## Validation

`engine_editor_foundation_tests` separately covers inspector metadata and
type-safe writes, every built-in World-to-Scene component, asset resolver
caching/invalidation, transient scene properties, stable descriptor identity,
malformed-scene diagnostics, perspective/orthographic picking, debug draw and
synchronized multi-threaded log-sink removal.

`engine_viewport_smoke --opengl` and `--vulkan` create independent views,
exercise zero-size clamping, same-size/invalid resize, target generations,
wrong-size rendering, destruction and ImGui backend validation on a real GPU.

Two interactive samples use the same executable for OpenGL and Vulkan:

```powershell
.\build\examples\sandbox\Debug\sample_editor_viewports.exe --opengl
.\build\examples\sandbox\Debug\sample_editor_viewports.exe --vulkan
.\build\examples\sandbox\Debug\sample_world_editor.exe --opengl
```

`sample_editor_viewports` displays simultaneously resizable perspective and
orthographic panels with picking. `sample_world_editor` demonstrates the
World bridge, hierarchy, generic reflected inspector, component enable state,
same-frame transform feedback, scene save/load, picking and the structured log
console. Add `--platform-viewports` to either sample for native ImGui windows,
or `--frames N` for automated smoke runs.
