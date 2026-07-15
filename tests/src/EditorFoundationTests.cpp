#include "engine/asset/AssetDescriptor.h"
#include "engine/asset/WorldSceneSerialization.h"
#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/render/Picking.h"
#include "engine/render/SceneRenderer.h"
#include "engine/runtime/Component.h"
#include "engine/runtime/RenderComponents.h"
#include "engine/runtime/WorldRenderBridge.h"
#include "engine/scene/DebugDraw.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace
{

using namespace engine;

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float left, float right, float epsilon = 0.001f)
{
    return std::abs(left - right) <= epsilon;
}

struct EditorTestComponent
{
    float Persistent = 2.0f;
    float RuntimeCache = 9.0f;
    float ReadOnly = 7.0f;
};

constexpr const char* kEditorTestComponent = "Test.EditorProperties";

void RegisterTestComponents(runtime::ComponentRegistry& registry)
{
    std::string error;
    Require(runtime::RegisterBuiltinRenderComponents(registry, &error), error);
    Require(registry.RegisterNative(kEditorTestComponent, 1,
        runtime::MakeDataComponentType<EditorTestComponent>(kEditorTestComponent),
        {
            runtime::MakeProperty("Persistent", &EditorTestComponent::Persistent,
                {"Persistent", runtime::PropertyFlags::Hidden}),
            runtime::MakeProperty("RuntimeCache", &EditorTestComponent::RuntimeCache,
                {"Runtime cache", runtime::PropertyFlags::Transient}),
            runtime::MakeProperty("ReadOnly", &EditorTestComponent::ReadOnly,
                {"Read only", runtime::PropertyFlags::ReadOnly |
                    runtime::PropertyFlags::Transient})
        }, &error), error);
}

void TestReflectionMetadataAndMutation()
{
    runtime::ComponentRegistry registry;
    RegisterTestComponents(registry);

    const std::string pointType(runtime::kPointLightComponent);
    const auto pointProperties = registry.Properties(pointType);
    const auto intensity = std::find_if(pointProperties.begin(), pointProperties.end(),
        [](const runtime::PropertyMetadata& property) {
            return property.Name == "Intensity";
        });
    Require(intensity != pointProperties.end() &&
                intensity->Type == runtime::PropertyType::FloatingPoint &&
                runtime::HasFlag(intensity->Flags, runtime::PropertyFlags::HasRange) &&
                intensity->Minimum == 0.0 && intensity->Maximum >= 1000000.0,
            "Point-light intensity must expose a constrained floating-point inspector property");

    const auto cameraProperties = registry.Properties(
        std::string(runtime::kCameraComponent));
    const auto projection = std::find_if(cameraProperties.begin(), cameraProperties.end(),
        [](const runtime::PropertyMetadata& property) {
            return property.Name == "Projection";
        });
    Require(projection != cameraProperties.end() &&
                projection->Type == runtime::PropertyType::Enumeration &&
                projection->EnumValues == std::vector<std::string>({"Perspective", "Orthographic"}),
            "Camera projection must expose enum labels for a generic inspector");

    runtime::World world(registry);
    const runtime::EntityId entity = world.CreateEntity("Inspector target");
    std::string error;
    Require(world.AddComponent(entity, pointType, &error), error);
    Require(world.SetComponentProperty(entity, pointType, "Intensity", 123.5, &error), error);
    runtime::PropertyValue value;
    Require(world.GetComponentProperty(entity, pointType, "Intensity", value, &error) &&
                Near(static_cast<float>(std::get<double>(value)), 123.5f),
            "Reflected numeric mutation must round-trip through World");
    Require(!world.SetComponentProperty(entity, pointType, "Intensity",
                                        std::string("wrong"), &error) &&
                error.find("wrong type") != std::string::npos,
            "Wrong inspector value types must return a useful error");

    Require(world.AddComponent(entity, kEditorTestComponent, &error), error);
    Require(!world.SetComponentProperty(entity, kEditorTestComponent,
                                        "ReadOnly", 1.0, &error) &&
                error.find("read-only") != std::string::npos,
            "Read-only metadata must be enforced by World property writes");
    Require(!world.SetComponentProperty(entity, kEditorTestComponent,
                                        "Missing", 1.0, &error) &&
                error.find("Unknown property") != std::string::npos,
            "Unknown inspector properties must return a useful error");
}

void TestWorldBridgeAllComponentsAndResolverCaching()
{
    runtime::ComponentRegistry registry;
    RegisterTestComponents(registry);
    runtime::World world(registry);
    std::string error;

    const assets::AssetGuid meshAsset{0x10, 0x11};
    const assets::AssetGuid replacementMeshAsset{0x12, 0x13};
    const assets::AssetGuid materialAsset{0x20, 0x21};
    const assets::AssetGuid cookieAsset{0x30, 0x31};
    const assets::AssetGuid replacementCookieAsset{0x32, 0x33};
    const assets::AssetGuid hdriAsset{0x40, 0x41};

    const runtime::EntityId root = world.CreateEntity("Root");
    world.SetLocalTransform(root, {{2.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
    const runtime::EntityId meshEntity = world.CreateEntity("Mesh");
    world.SetParent(meshEntity, root);
    world.SetLocalTransform(meshEntity, {{0.0f, 1.0f, -3.0f}, {}, {1.0f, 1.0f, 1.0f}});
    Require(world.AddComponent(meshEntity, std::string(runtime::kMeshRendererComponent), &error), error);
    auto* mesh = world.GetComponent<runtime::MeshRendererComponent>(
        meshEntity, std::string(runtime::kMeshRendererComponent));
    mesh->MeshAsset = meshAsset;
    mesh->MaterialAsset = materialAsset;
    mesh->AlwaysVisible = true;

    auto addLight = [&](std::string_view type, const glm::vec3& position) {
        const runtime::EntityId entity = world.CreateEntity(std::string(type));
        world.SetLocalTransform(entity, {position, {}, {1.0f, 1.0f, 1.0f}});
        Require(world.AddComponent(entity, std::string(type), &error), error);
        return entity;
    };
    const runtime::EntityId pointEntity = addLight(runtime::kPointLightComponent, {-2.0f, 3.0f, 0.0f});
    const runtime::EntityId spotEntity = addLight(runtime::kSpotLightComponent, {0.0f, 4.0f, 2.0f});
    const runtime::EntityId areaEntity = addLight(runtime::kAreaLightComponent, {2.0f, 3.0f, 0.0f});
    world.GetComponent<runtime::PointLightComponent>(pointEntity,
        std::string(runtime::kPointLightComponent))->CookieAsset = cookieAsset;
    world.GetComponent<runtime::SpotLightComponent>(spotEntity,
        std::string(runtime::kSpotLightComponent))->CookieAsset = cookieAsset;
    world.GetComponent<runtime::AreaLightComponent>(areaEntity,
        std::string(runtime::kAreaLightComponent))->CookieAsset = cookieAsset;

    const runtime::EntityId sunEntity = world.CreateEntity("Sun");
    Require(world.AddComponent(sunEntity, std::string(runtime::kDirectionalLightComponent), &error), error);
    world.GetComponent<runtime::DirectionalLightComponent>(sunEntity,
        std::string(runtime::kDirectionalLightComponent))->Intensity = 4.25f;

    const runtime::EntityId cameraEntity = world.CreateEntity("Camera");
    world.SetLocalTransform(cameraEntity, {{0.0f, 2.0f, 8.0f}, {}, {1.0f, 1.0f, 1.0f}});
    Require(world.AddComponent(cameraEntity, std::string(runtime::kCameraComponent), &error), error);
    auto* cameraComponent = world.GetComponent<runtime::CameraComponent>(
        cameraEntity, std::string(runtime::kCameraComponent));
    cameraComponent->Primary = true;
    cameraComponent->Projection = CameraProjection::Orthographic;
    cameraComponent->OrthographicSize = 17.0f;

    const runtime::EntityId environmentEntity = world.CreateEntity("Environment");
    Require(world.AddComponent(environmentEntity, std::string(runtime::kEnvironmentComponent), &error), error);
    auto* environment = world.GetComponent<runtime::EnvironmentComponent>(
        environmentEntity, std::string(runtime::kEnvironmentComponent));
    environment->Source = EnvironmentSource::EquirectangularHdr;
    environment->HdriAsset = hdriAsset;
    environment->ExposureEV = 1.5f;
    environment->VisibleBackground = false;

    int meshLoads = 0;
    int materialLoads = 0;
    int textureLoads = 0;
    int hdriLoads = 0;
    runtime::WorldRenderResolvers resolvers;
    resolvers.Mesh = [&](assets::AssetGuid) {
        ++meshLoads;
        return std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    };
    resolvers.Material = [&](assets::AssetGuid) -> std::optional<Material> {
        ++materialLoads;
        Material material;
        material.Metallic = 0.8f;
        material.Roughness = 0.2f;
        return material;
    };
    resolvers.Texture = [&](assets::AssetGuid) {
        ++textureLoads;
        return textures::MakeLightCookie(32);
    };
    resolvers.Hdri = [&](assets::AssetGuid) {
        ++hdriLoads;
        auto image = std::make_shared<HdrImageData>();
        image->Width = 2;
        image->Height = 1;
        image->Pixels.assign(6, 1.0f);
        return image;
    };

    runtime::WorldRenderBridge bridge(std::move(resolvers));
    Scene scene;
    Camera camera;
    const runtime::WorldRenderSyncResult first = bridge.Synchronize(world, scene, &camera);
    Require(first.MeshRenderers == 1 && first.PointLights == 1 &&
                first.SpotLights == 1 && first.AreaLights == 1 &&
                first.DirectionalLight == sunEntity && first.ActiveCamera == cameraEntity &&
                first.Environment == environmentEntity,
            "World bridge must translate every built-in render component");
    Require(scene.Instances().size() == 1 &&
                Near(scene.Instances()[0].Transform[3].x, 2.0f) &&
                Near(scene.Instances()[0].Transform[3].y, 1.0f) &&
                Near(scene.Instances()[0].Mat.Metallic, 0.8f) &&
                scene.Instances()[0].SourceEntity == meshEntity &&
                scene.PointLights()[0].Cookie && scene.SpotLights()[0].Cookie &&
                scene.AreaLights()[0].Cookie && Near(scene.Sun.Intensity, 4.25f) &&
                camera.Projection == CameraProjection::Orthographic &&
                Near(camera.OrthographicSize, 17.0f) &&
                scene.Environment.Hdri && !scene.Sky.VisibleBackground,
            "World bridge must preserve transforms, assets, lights, camera and environment values");
    Require(meshLoads == 1 && materialLoads == 1 && textureLoads == 3 && hdriLoads == 1,
            "Initial World bridge synchronization must resolve each component asset once");

    bridge.Synchronize(world, scene, &camera);
    Require(meshLoads == 1 && materialLoads == 1 && textureLoads == 3 && hdriLoads == 1,
            "Unchanged World bridge synchronization must reuse resolver caches");

    mesh->MeshAsset = replacementMeshAsset;
    world.GetComponent<runtime::PointLightComponent>(pointEntity,
        std::string(runtime::kPointLightComponent))->CookieAsset = replacementCookieAsset;
    bridge.Synchronize(world, scene, &camera);
    Require(meshLoads == 2 && textureLoads == 4,
            "Changed asset GUIDs must invalidate only the affected resolver caches");

    Require(world.SetComponentEnabled(areaEntity, std::string(runtime::kAreaLightComponent), false) &&
                world.SetComponentEnabled(sunEntity, std::string(runtime::kDirectionalLightComponent), false) &&
                world.SetComponentEnabled(environmentEntity,
                    std::string(runtime::kEnvironmentComponent), false),
            "Render component enable state must be editable");
    const runtime::WorldRenderSyncResult disabled = bridge.Synchronize(world, scene, &camera);
    Require(disabled.AreaLights == 0 && disabled.DirectionalLight == runtime::kInvalidEntity &&
                disabled.Environment == runtime::kInvalidEntity && scene.AreaLights().empty() &&
                scene.Environment.Source == EnvironmentSource::ProceduralSky,
            "Disabled World components must clear their renderer Scene state immediately");
}

void TestSceneSerializationRoundTripAndDiagnostics()
{
    runtime::ComponentRegistry registry;
    RegisterTestComponents(registry);
    runtime::World world(registry);
    std::string error;

    const assets::AssetGuid rootGuid{0x101, 0x201};
    const assets::AssetGuid childGuid{0x102, 0x202};
    const runtime::EntityId root = world.CreateEntityWithGuid(rootGuid, "Root");
    const runtime::EntityId child = world.CreateEntityWithGuid(childGuid, "Child");
    Require(root != runtime::kInvalidEntity && child != runtime::kInvalidEntity &&
                world.SetParent(child, root), "Stable-GUID scene hierarchy must be constructible");
    world.SetTag(child, "Gameplay");
    world.SetLayer(child, 7);
    world.SetActive(child, false);
    world.SetLocalTransform(child, {{1.0f, 2.0f, 3.0f},
        glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        {2.0f, 3.0f, 4.0f}});
    Require(world.AddComponent(child, kEditorTestComponent, &error), error);
    auto* editorData = world.GetComponent<EditorTestComponent>(child, kEditorTestComponent);
    editorData->Persistent = 42.5f;
    editorData->RuntimeCache = 99.0f;
    world.SetComponentEnabled(child, kEditorTestComponent, false);

    assets::SceneAssetData serialized;
    serialized.Layers = {"Default", "Gameplay"};
    serialized.EditorMetadata["viewport"] = "perspective";
    Require(assets::SerializeWorld(world, serialized, child, &error), error);
    Require(serialized.Objects.size() == 2 && serialized.Layers.size() == 2 &&
                serialized.EditorMetadata["viewport"] == "perspective",
            "World serialization must preserve scene-level metadata");
    const auto childRecord = std::find_if(serialized.Objects.begin(), serialized.Objects.end(),
        [&](const assets::SceneObjectRecord& object) { return object.Guid == childGuid; });
    Require(childRecord != serialized.Objects.end() && childRecord->Parent == rootGuid &&
                childRecord->Tag == "Gameplay" && childRecord->Layer == 7 && !childRecord->Active,
            "World serialization must preserve entity hierarchy and editor attributes");
    Require(childRecord->Components.size() == 1 &&
                childRecord->Components[0].Properties.contains("Persistent") &&
                !childRecord->Components[0].Properties.contains("RuntimeCache") &&
                !childRecord->Components[0].Properties.contains("ReadOnly"),
            "Transient reflection properties must not be persisted");

    runtime::World loaded(registry);
    runtime::EntityId loadedCamera = runtime::kInvalidEntity;
    Require(assets::DeserializeWorld(serialized, loaded, &loadedCamera, &error), error);
    const runtime::EntityId loadedChild = loaded.FindEntity(childGuid);
    const auto* loadedData = loaded.GetComponent<EditorTestComponent>(
        loadedChild, kEditorTestComponent);
    Require(loadedChild != runtime::kInvalidEntity &&
                loaded.GetGuid(loaded.GetParent(loadedChild)) == rootGuid &&
                loaded.GetTag(loadedChild) == "Gameplay" && loaded.GetLayer(loadedChild) == 7 &&
                !loaded.IsComponentEnabled(loadedChild, kEditorTestComponent) &&
                loadedData && Near(loadedData->Persistent, 42.5f) &&
                Near(loadedData->RuntimeCache, 9.0f) &&
                loaded.GetGuid(loadedCamera) == childGuid,
            "World scene in-memory round-trip must preserve serialized state and reset transient state");

    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("engine_editor_foundation_" + unique + ".sla-scene");
    std::filesystem::remove(path);
    Require(assets::SaveWorldScene(path, world, child, &error), error);
    assets::AssetDescriptor firstDescriptor;
    Require(assets::LoadAssetDescriptor(path, firstDescriptor, &error), error);
    world.SetName(child, "Renamed Child");
    Require(assets::SaveWorldScene(path, world, child, &error), error);
    assets::AssetDescriptor secondDescriptor;
    Require(assets::LoadAssetDescriptor(path, secondDescriptor, &error), error);
    runtime::World fileLoaded(registry);
    Require(firstDescriptor.Guid == secondDescriptor.Guid &&
                assets::LoadWorldScene(path, fileLoaded, nullptr, &error) &&
                fileLoaded.GetName(fileLoaded.FindEntity(childGuid)) == "Renamed Child",
            "Repeated scene saves must preserve descriptor identity and update World data");
    std::filesystem::remove(path);

    assets::SceneAssetData malformed = serialized;
    malformed.Objects.push_back(malformed.Objects.front());
    runtime::World rejected(registry);
    Require(!assets::DeserializeWorld(malformed, rejected, nullptr, &error) &&
                error.find("duplicate") != std::string::npos,
            "Duplicate entity GUIDs must be rejected with a useful diagnostic");
    malformed = serialized;
    malformed.Objects.back().Parent = {0xdead, 0xbeef};
    Require(!assets::DeserializeWorld(malformed, rejected, nullptr, &error) &&
                error.find("missing parent") != std::string::npos &&
                error.find(malformed.Objects.back().Name) != std::string::npos,
            "Missing parents must identify the affected entity");
    malformed = serialized;
    malformed.Objects.back().Components.front().Properties["UnknownProperty"] = "1";
    Require(!assets::DeserializeWorld(malformed, rejected, nullptr, &error) &&
                error.find("UnknownProperty") != std::string::npos &&
                error.find(kEditorTestComponent) != std::string::npos,
            "Unknown component properties must identify entity, component and property");
}

void TestPerspectiveAndOrthographicPicking()
{
    Scene scene;
    auto cube = std::make_shared<MeshData>(primitives::MakeCube(0.75f));
    Material material;
    scene.AddInstance(cube, material, glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, 0.0f}));
    scene.Instances().back().SourceEntity = 11;
    scene.AddInstance(cube, material, glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -4.0f}));
    scene.Instances().back().SourceEntity = 22;

    Camera camera;
    camera.Position = {0.0f, 0.0f, 6.0f};
    camera.Yaw = -90.0f;
    camera.Pitch = 0.0f;
    PickingResult result = PickScene(scene, camera, 400.0f, 300.0f, 800, 600);
    Require(result.Hit && result.Entity == 11 && result.InstanceIndex == 0,
            "Perspective picking must select the nearest intersected entity");
    ApplyPickingSelection(scene, result);
    Require(scene.SelectedEntity == 11, "Picking result must update Scene selection");

    camera.Projection = CameraProjection::Orthographic;
    camera.OrthographicSize = 8.0f;
    result = PickScene(scene, camera, 400.0f, 300.0f, 800, 600);
    Require(result.Hit && result.Entity == 11,
            "Orthographic picking rays must select centered geometry");
    const PickingRay fallback = BuildPickingRay(camera, 0.0f, 0.0f, 0, 0);
    Require(Near(glm::length(fallback.Direction), 1.0f),
            "Zero-sized viewports must produce a finite fallback picking ray");
    result = PickScene(scene, camera, 799.0f, 0.0f, 800, 600);
    ApplyPickingSelection(scene, result);
    Require(!result.Hit && scene.SelectedEntity == 0,
            "Empty orthographic viewport clicks must clear selection");
}

void TestDebugDrawPreparation()
{
    Scene scene;
    scene.Visibility.Enabled = false;
    auto cube = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    scene.AddInstance(cube, Material{}, glm::mat4(1.0f));
    scene.Instances().back().SourceEntity = 77;
    scene.SelectedEntity = 77;
    scene.DebugDraw().Line({0, 0, 0}, {1, 0, 0}, {1, 0, 0}, DebugDepthMode::Overlay);
    scene.DebugDraw().Aabb({-1, -1, -1}, {1, 1, 1}, {0, 1, 0});
    scene.DebugDraw().Grid(2.0f, 1.0f);
    scene.DebugDraw().Icon(DebugIconType::PointLight, {0, 2, 0}, {1, 1, 0}, 0.5f);
    scene.DebugDraw().Icon(DebugIconType::Camera, {2, 2, 0}, {0, 1, 1}, 0.5f,
                           DebugDepthMode::DepthTested);

    Camera camera;
    camera.Position = {0.0f, 2.0f, 7.0f};
    SceneRenderer renderer;
    const RenderFrameData& frame = renderer.PrepareFrame(scene, camera, 640, 360);
    const bool hasOverlay = std::any_of(frame.DebugLines.begin(), frame.DebugLines.end(),
        [](const DebugLine& line) { return line.Depth == DebugDepthMode::Overlay; });
    const bool hasDepth = std::any_of(frame.DebugLines.begin(), frame.DebugLines.end(),
        [](const DebugLine& line) { return line.Depth == DebugDepthMode::DepthTested; });
    Require(frame.DebugLines.size() >= 40 && hasOverlay && hasDepth,
            "Debug preparation must expand grid, AABB, icons and selection in both depth modes");
    scene.DebugDraw().Clear();
    const RenderFrameData& selectedOnly = renderer.PrepareFrame(scene, camera, 640, 360);
    Require(selectedOnly.DebugLines.size() == 12,
            "Selected mesh instances must produce a 12-edge highlight AABB");
}

void TestConcurrentLogSinkRemoval()
{
    std::promise<void> enteredPromise;
    std::shared_future<void> entered = enteredPromise.get_future().share();
    std::promise<void> releasePromise;
    std::shared_future<void> release = releasePromise.get_future().share();
    std::atomic<int> callbacks{0};
    const log::SinkId sink = log::AddSink([&](const log::Record&) {
        if (callbacks.fetch_add(1, std::memory_order_relaxed) == 0)
            enteredPromise.set_value();
        release.wait();
    });
    Require(sink != 0, "Concurrent log sink must register");

    std::thread writer([] { log::Info("EditorTest", "blocking sink callback"); });
    Require(entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
            "Log sink callback must start on the writer thread");
    auto removal = std::async(std::launch::async, [sink] { return log::RemoveSink(sink); });
    Require(removal.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
            "External sink removal must wait for an in-flight callback");
    releasePromise.set_value();
    writer.join();
    Require(removal.get(), "Concurrent sink removal must complete after callback exit");
    log::Info("EditorTest", "after synchronized removal");
    Require(callbacks.load(std::memory_order_relaxed) == 1,
            "Removed console sinks must never receive later records");
}

} // namespace

int main()
{
    try
    {
        TestReflectionMetadataAndMutation();
        TestWorldBridgeAllComponentsAndResolverCaching();
        TestSceneSerializationRoundTripAndDiagnostics();
        TestPerspectiveAndOrthographicPicking();
        TestDebugDrawPreparation();
        TestConcurrentLogSinkRemoval();
        std::cout << "All editor foundation tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Editor foundation test failure: " << exception.what() << '\n';
        return 1;
    }
}
