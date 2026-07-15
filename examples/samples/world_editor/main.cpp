#include "engine/asset/WorldSceneSerialization.h"
#include "engine/core/Application.h"
#include "engine/core/Log.h"
#include "engine/editor/ImGuiLayer.h"
#include "engine/render/Picking.h"
#include "engine/runtime/RenderComponents.h"
#include "engine/scene/Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace
{

using namespace engine;

glm::quat LookRotation(const glm::vec3& position, const glm::vec3& target)
{
    return glm::quat_cast(glm::inverse(glm::lookAt(
        position, target, glm::vec3(0.0f, 1.0f, 0.0f))));
}

runtime::EntityId AddMeshEntity(runtime::World& world, std::string name,
                                std::shared_ptr<MeshData> mesh,
                                const Material& material,
                                const runtime::Transform& transform,
                                std::string& error)
{
    const runtime::EntityId entity = world.CreateEntity(std::move(name));
    world.SetLocalTransform(entity, transform);
    if (!world.AddComponent(entity, std::string(runtime::kMeshRendererComponent), &error))
        throw std::runtime_error(error);
    auto* renderer = world.GetComponent<runtime::MeshRendererComponent>(
        entity, std::string(runtime::kMeshRendererComponent));
    renderer->Mesh = std::move(mesh);
    renderer->Mat = material;
    return entity;
}

struct SampleEntities
{
    assets::AssetGuid AnimatedCube;
    assets::AssetGuid Camera;
};

SampleEntities PopulateWorld(runtime::World& world)
{
    std::string error;
    Material floor;
    floor.Albedo = {0.09f, 0.12f, 0.16f};
    floor.Roughness = 0.78f;
    AddMeshEntity(world, "Studio Floor",
        std::make_shared<MeshData>(primitives::MakePlane(20.0f, 8)), floor,
        {{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}}, error);

    Material blue;
    blue.Albedo = {0.06f, 0.28f, 0.82f};
    blue.Metallic = 0.72f;
    blue.Roughness = 0.22f;
    const runtime::EntityId cube = AddMeshEntity(world, "Animated Cube",
        std::make_shared<MeshData>(primitives::MakeCube(1.0f)), blue,
        {{-1.8f, 1.1f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}}, error);

    Material orange;
    orange.Albedo = {0.92f, 0.18f, 0.035f};
    orange.Metallic = 0.12f;
    orange.Roughness = 0.34f;
    AddMeshEntity(world, "Orange Sphere",
        std::make_shared<MeshData>(primitives::MakeSphere(1.15f, 32, 32)), orange,
        {{1.8f, 1.2f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}}, error);

    const runtime::EntityId point = world.CreateEntity("Warm Point Light");
    world.SetLocalTransform(point, {{-3.0f, 4.0f, 2.5f}, {}, {1.0f, 1.0f, 1.0f}});
    if (!world.AddComponent(point, std::string(runtime::kPointLightComponent), &error))
        throw std::runtime_error(error);
    auto* pointLight = world.GetComponent<runtime::PointLightComponent>(
        point, std::string(runtime::kPointLightComponent));
    pointLight->Color = {1.0f, 0.3f, 0.08f};
    pointLight->Intensity = 70.0f;
    pointLight->Radius = 18.0f;
    pointLight->CastsShadows = true;

    const glm::vec3 spotPosition{3.5f, 5.0f, 4.0f};
    const runtime::EntityId spot = world.CreateEntity("Cool Spot Light");
    world.SetLocalTransform(spot, {spotPosition,
        LookRotation(spotPosition, {0.8f, 0.0f, 0.0f}), {1.0f, 1.0f, 1.0f}});
    if (!world.AddComponent(spot, std::string(runtime::kSpotLightComponent), &error))
        throw std::runtime_error(error);
    auto* spotLight = world.GetComponent<runtime::SpotLightComponent>(
        spot, std::string(runtime::kSpotLightComponent));
    spotLight->Color = {0.1f, 0.42f, 1.0f};
    spotLight->Intensity = 95.0f;
    spotLight->Range = 22.0f;
    spotLight->CastsShadows = true;
    spotLight->Cookie = textures::MakeLightCookie(128);

    const glm::vec3 sunPosition{0.0f, 10.0f, 0.0f};
    const runtime::EntityId sun = world.CreateEntity("Directional Sun");
    world.SetLocalTransform(sun, {sunPosition,
        LookRotation(sunPosition, {-4.0f, 0.0f, -3.0f}), {1.0f, 1.0f, 1.0f}});
    if (!world.AddComponent(sun, std::string(runtime::kDirectionalLightComponent), &error))
        throw std::runtime_error(error);

    const runtime::EntityId environment = world.CreateEntity("Environment");
    if (!world.AddComponent(environment, std::string(runtime::kEnvironmentComponent), &error))
        throw std::runtime_error(error);
    auto* environmentData = world.GetComponent<runtime::EnvironmentComponent>(
        environment, std::string(runtime::kEnvironmentComponent));
    environmentData->SkyIntensity = 0.55f;
    environmentData->StarDensity = 0.0018f;
    environmentData->MilkyWayIntensity = 0.45f;

    const glm::vec3 cameraPosition{7.0f, 5.0f, 10.0f};
    const runtime::EntityId camera = world.CreateEntity("Editor Camera");
    world.SetLocalTransform(camera, {cameraPosition,
        LookRotation(cameraPosition, {0.0f, 1.0f, 0.0f}), {1.0f, 1.0f, 1.0f}});
    if (!world.AddComponent(camera, std::string(runtime::kCameraComponent), &error))
        throw std::runtime_error(error);
    world.GetComponent<runtime::CameraComponent>(
        camera, std::string(runtime::kCameraComponent))->Primary = true;

    return {world.GetGuid(cube), world.GetGuid(camera)};
}

void RestoreProceduralMeshes(runtime::World& world)
{
    for (const runtime::EntityInfo& entity : world.ListEntities())
    {
        auto* renderer = world.GetComponent<runtime::MeshRendererComponent>(
            entity.Id, std::string(runtime::kMeshRendererComponent));
        if (!renderer)
            continue;
        if (entity.Name == "Studio Floor")
            renderer->Mesh = std::make_shared<MeshData>(primitives::MakePlane(20.0f, 8));
        else if (entity.Name == "Animated Cube")
            renderer->Mesh = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
        else if (entity.Name == "Orange Sphere")
            renderer->Mesh = std::make_shared<MeshData>(primitives::MakeSphere(1.15f, 32, 32));
    }
}

bool DrawProperty(runtime::World& world, runtime::EntityId entity,
                  const runtime::ConstComponentView& component,
                  const runtime::PropertyMetadata& property)
{
    if (runtime::HasFlag(property.Flags, runtime::PropertyFlags::Hidden))
        return false;
    runtime::PropertyValue value;
    std::string error;
    if (!world.GetComponentProperty(entity, component.TypeName,
                                    property.Name, value, &error))
        return false;

    bool changed = false;
    ImGui::PushID(property.Name.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(property.DisplayName.c_str());
    ImGui::SameLine(150.0f);
    ImGui::SetNextItemWidth(-1.0f);
    switch (property.Type)
    {
    case runtime::PropertyType::Boolean:
    {
        bool edited = std::get<bool>(value);
        changed = ImGui::Checkbox("##value", &edited);
        if (changed) value = edited;
        break;
    }
    case runtime::PropertyType::FloatingPoint:
    {
        float edited = static_cast<float>(std::get<double>(value));
        if (runtime::HasFlag(property.Flags, runtime::PropertyFlags::HasRange))
            changed = ImGui::SliderFloat("##value", &edited,
                static_cast<float>(property.Minimum), static_cast<float>(property.Maximum));
        else
            changed = ImGui::DragFloat("##value", &edited,
                property.Step > 0.0 ? static_cast<float>(property.Step) : 0.1f);
        if (changed) value = static_cast<double>(edited);
        break;
    }
    case runtime::PropertyType::Vector2:
    {
        glm::vec2 edited = std::get<glm::vec2>(value);
        changed = ImGui::DragFloat2("##value", &edited.x,
            property.Step > 0.0 ? static_cast<float>(property.Step) : 0.05f);
        if (changed) value = edited;
        break;
    }
    case runtime::PropertyType::Vector3:
    {
        glm::vec3 edited = std::get<glm::vec3>(value);
        changed = runtime::HasFlag(property.Flags, runtime::PropertyFlags::Color)
            ? ImGui::ColorEdit3("##value", &edited.x,
                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)
            : ImGui::DragFloat3("##value", &edited.x,
                property.Step > 0.0 ? static_cast<float>(property.Step) : 0.05f);
        if (changed) value = edited;
        break;
    }
    case runtime::PropertyType::Vector4:
    {
        glm::vec4 edited = std::get<glm::vec4>(value);
        changed = ImGui::DragFloat4("##value", &edited.x, 0.05f);
        if (changed) value = edited;
        break;
    }
    case runtime::PropertyType::Quaternion:
    {
        glm::quat edited = std::get<glm::quat>(value);
        std::array<float, 4> components{edited.x, edited.y, edited.z, edited.w};
        changed = ImGui::DragFloat4("##value", components.data(), 0.01f);
        if (changed)
            value = glm::normalize(glm::quat(components[3], components[0],
                                             components[1], components[2]));
        break;
    }
    case runtime::PropertyType::Enumeration:
    {
        int selected = static_cast<int>(std::get<int64_t>(value));
        const char* preview = selected >= 0 && selected < static_cast<int>(property.EnumValues.size())
            ? property.EnumValues[static_cast<size_t>(selected)].c_str() : "Unknown";
        if (ImGui::BeginCombo("##value", preview))
        {
            for (int index = 0; index < static_cast<int>(property.EnumValues.size()); ++index)
            {
                if (ImGui::Selectable(property.EnumValues[static_cast<size_t>(index)].c_str(),
                                      selected == index))
                {
                    selected = index;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (changed) value = static_cast<int64_t>(selected);
        break;
    }
    default:
        ImGui::TextUnformatted(runtime::PropertyValueToString(property.Type, value).c_str());
        break;
    }
    ImGui::PopID();
    if (changed && !runtime::HasFlag(property.Flags, runtime::PropertyFlags::ReadOnly) &&
        !world.SetComponentProperty(entity, component.TypeName, property.Name, value, &error))
        log::Warn("Inspector", error);
    return changed;
}

void DrawEntityNode(runtime::World& world, runtime::EntityId entity,
                    runtime::EntityId& selection)
{
    const std::vector<runtime::EntityId> children = world.GetChildren(entity);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (selection == entity) flags |= ImGuiTreeNodeFlags_Selected;
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(entity)),
                                        flags, "%s", world.GetName(entity).c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        selection = entity;
    if (open)
    {
        for (runtime::EntityId child : children)
            DrawEntityNode(world, child, selection);
        ImGui::TreePop();
    }
}

struct ConsoleState
{
    std::mutex Mutex;
    std::deque<log::Record> Records;
};

struct ScopedLogSink
{
    log::SinkId Id = 0;
    ~ScopedLogSink()
    {
        if (Id != 0)
            log::RemoveSink(Id);
    }
};

void DrawConsole(ConsoleState& console)
{
    ImGui::Begin("Console");
    if (ImGui::Button("Clear"))
    {
        std::scoped_lock lock(console.Mutex);
        console.Records.clear();
    }
    ImGui::Separator();
    std::vector<log::Record> records;
    {
        std::scoped_lock lock(console.Mutex);
        records.assign(console.Records.begin(), console.Records.end());
    }
    ImGui::BeginChild("ConsoleRecords", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const log::Record& record : records)
    {
        const ImVec4 color = record.Severity == log::Level::Error
            ? ImVec4(1.0f, 0.3f, 0.25f, 1.0f)
            : record.Severity == log::Level::Warn
                ? ImVec4(1.0f, 0.72f, 0.2f, 1.0f)
                : ImVec4(0.78f, 0.82f, 0.9f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("#%llu [%s] %s",
            static_cast<unsigned long long>(record.Sequence),
            record.Category.c_str(), record.Message.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        ApplicationDesc description;
        description.Window.title = "World editor foundation sample";
        description.Window.width = 1500;
        description.Window.height = 900;
        description.EnableImGui = true;
        description.EnableRuntimeMonitors = false;
        description.SynchronizeWorldToScene = true;
        int maximumFrames = 0;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);
            if (argument == "--vulkan") description.Window.api = GraphicsApi::Vulkan;
            else if (argument == "--opengl") description.Window.api = GraphicsApi::OpenGL;
            else if (argument == "--platform-viewports")
                description.EnableImGuiPlatformViewports = true;
            else if (argument == "--frames" && index + 1 < argc)
                maximumFrames = std::max(std::atoi(argv[++index]), 1);
        }

        Application app(description);
        editor::ImGuiLayer* ui = app.GetImGuiLayer();
        if (!ui)
            throw std::runtime_error("ImGui layer was not initialized");
        runtime::World& world = app.GetWorld();
        const SampleEntities entities = PopulateWorld(world);
        runtime::EntityId selection = world.FindEntity(entities.AnimatedCube);
        app.GetScene().PostProcess.AutoExposure = false;
        app.GetScene().Visibility.GpuOcclusionCulling = false;
        app.GetScene().DebugDraw().Grid(10.0f, 1.0f, {0.0f, 0.01f, 0.0f});

        const RenderViewportHandle viewport = app.GetSceneRenderer().CreateViewport(
            app.GetBackend(), {900, 600, "World Scene"});
        if (!viewport)
            throw std::runtime_error("Could not create World editor viewport");
        uint32_t viewportWidth = 900;
        uint32_t viewportHeight = 600;
        const std::filesystem::path scenePath =
            std::filesystem::path(".cache") / "samples" / "world_editor.sla-scene";
        std::filesystem::create_directories(scenePath.parent_path());

        ConsoleState console;
        const log::SinkId consoleSink = log::AddSink([&](const log::Record& record) {
            std::scoped_lock lock(console.Mutex);
            console.Records.push_back(record);
            while (console.Records.size() > 500)
                console.Records.pop_front();
        });
        ScopedLogSink sinkLifetime{consoleSink};
        log::Info("WorldEditor", "Console sink connected");

        float elapsed = 0.0f;
        int frameCount = 0;
        bool animateCube = true;
        app.SetUpdateCallback([&](float deltaSeconds) {
            elapsed += deltaSeconds;
            const runtime::EntityId cube = world.FindEntity(entities.AnimatedCube);
            if (animateCube && cube != runtime::kInvalidEntity)
            {
                runtime::Transform transform = world.GetLocalTransform(cube);
                transform.Rotation = glm::angleAxis(elapsed * 0.65f,
                    glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)));
                world.SetLocalTransform(cube, transform);
            }
            if (maximumFrames > 0 && ++frameCount >= maximumFrames)
                app.RequestQuit();
        });

        app.SetImGuiCallback([&] {
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                ImGuiDockNodeFlags_PassthruCentralNode);

            ImGui::Begin("Hierarchy");
            ImGui::Text("Backend: %s", app.GetBackend().Name());
            if (ImGui::Button("Save scene"))
            {
                std::string error;
                const runtime::EntityId camera = world.FindEntity(entities.Camera);
                if (assets::SaveWorldScene(scenePath, world, camera, &error))
                    log::Info("WorldEditor", "Saved " + scenePath.string());
                else
                    log::Error("WorldEditor", error);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load scene"))
            {
                std::string error;
                runtime::EntityId loadedCamera = runtime::kInvalidEntity;
                if (assets::LoadWorldScene(scenePath, world, &loadedCamera, &error))
                {
                    RestoreProceduralMeshes(world);
                    selection = loadedCamera != runtime::kInvalidEntity
                        ? loadedCamera : world.FindEntity(entities.AnimatedCube);
                    log::Info("WorldEditor", "Loaded " + scenePath.string());
                }
                else
                    log::Error("WorldEditor", error);
            }
            ImGui::Checkbox("Animate cube", &animateCube);
            ImGui::Separator();
            for (const runtime::EntityInfo& entity : world.ListEntities())
                if (entity.Parent == runtime::kInvalidEntity)
                    DrawEntityNode(world, entity.Id, selection);
            ImGui::End();

            ImGui::Begin("Inspector");
            if (selection != runtime::kInvalidEntity && world.IsAlive(selection))
            {
                std::string name = world.GetName(selection);
                std::array<char, 256> nameBuffer{};
                std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", name.c_str());
                if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    world.SetName(selection, nameBuffer.data());
                runtime::Transform transform = world.GetLocalTransform(selection);
                bool transformChanged = ImGui::DragFloat3("Position", &transform.Position.x, 0.05f);
                transformChanged |= ImGui::DragFloat3("Scale", &transform.Scale.x, 0.05f, 0.01f, 100.0f);
                if (transformChanged)
                    world.SetLocalTransform(selection, transform);
                for (const runtime::ConstComponentView& component :
                     static_cast<const runtime::World&>(world).ListComponents(selection))
                {
                    ImGui::PushID(component.TypeName.c_str());
                    bool enabled = component.Enabled;
                    if (ImGui::Checkbox("##enabled", &enabled))
                        world.SetComponentEnabled(selection, component.TypeName, enabled);
                    ImGui::SameLine();
                    const bool open = ImGui::CollapsingHeader(component.TypeName.c_str(),
                                                               ImGuiTreeNodeFlags_DefaultOpen);
                    if (open)
                    {
                        for (const runtime::PropertyMetadata& property :
                             world.Components().Properties(component.TypeName))
                            DrawProperty(world, selection, component, property);
                    }
                    ImGui::PopID();
                }
            }
            else
                ImGui::TextUnformatted("Select an entity in the hierarchy or viewport.");
            ImGui::End();

            // Inspector edits happen during this callback, after Application's
            // normal bridge update. Synchronize once more so the viewport
            // reflects a dragged property in the same displayed frame.
            app.GetWorldRenderBridge().Synchronize(world, app.GetScene(), &app.GetCamera());
            app.GetScene().SelectedEntity = selection;

            ImGui::Begin("World Scene");
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const uint32_t requestedWidth = static_cast<uint32_t>(std::max(available.x, 1.0f));
            const uint32_t requestedHeight = static_cast<uint32_t>(std::max(available.y, 1.0f));
            if ((requestedWidth != viewportWidth || requestedHeight != viewportHeight) &&
                app.GetSceneRenderer().ResizeViewport(app.GetBackend(), viewport,
                                                      requestedWidth, requestedHeight))
            {
                viewportWidth = requestedWidth;
                viewportHeight = requestedHeight;
            }
            app.GetSceneRenderer().RenderViewport(app.GetBackend(), viewport,
                app.GetScene(), app.GetCamera(), viewportWidth, viewportHeight, elapsed);
            const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
            ui->DrawViewportImage(app.GetBackend().GetViewportTexture(viewport),
                ImVec2(static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)));
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const PickingResult hit = PickScene(app.GetScene(), app.GetCamera(),
                    mouse.x - imageOrigin.x, mouse.y - imageOrigin.y,
                    viewportWidth, viewportHeight);
                ApplyPickingSelection(app.GetScene(), hit);
                selection = hit.Hit ? static_cast<runtime::EntityId>(hit.Entity)
                                    : runtime::kInvalidEntity;
            }
            ImGui::End();
            DrawConsole(console);
        });

        app.Run();
        if (!log::RemoveSink(consoleSink))
            throw std::runtime_error("Could not remove World editor console sink");
        sinkLifetime.Id = 0;
        app.GetBackend().WaitIdle();
        ui->ForgetTexture(viewport);
        app.GetSceneRenderer().DestroyViewport(app.GetBackend(), viewport);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "%s\n", exception.what());
        return 1;
    }
}
