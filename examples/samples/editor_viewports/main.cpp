#include "engine/core/Application.h"
#include "engine/editor/ImGuiLayer.h"
#include "engine/render/Picking.h"
#include "engine/scene/Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string_view>

#include <glm/gtc/matrix_transform.hpp>

namespace
{

using namespace engine;

void LookAt(Camera& camera, const glm::vec3& target)
{
    const glm::vec3 direction = glm::normalize(target - camera.Position);
    camera.Pitch = glm::degrees(std::asin(glm::clamp(direction.y, -1.0f, 1.0f)));
    camera.Yaw = glm::degrees(std::atan2(direction.z, direction.x));
}

void PopulateScene(Scene& scene)
{
    scene.PostProcess.AutoExposure = false;
    scene.PostProcess.Exposure = 1.0f;
    scene.Visibility.GpuOcclusionCulling = false;
    scene.Sky.EnableDayNightCycle = true;
    scene.Sky.StarDensity = 0.0018f;
    scene.Sun.Direction = glm::normalize(glm::vec3(-0.45f, -0.8f, -0.3f));

    Material floor;
    floor.Albedo = {0.12f, 0.15f, 0.2f};
    floor.Metallic = 0.05f;
    floor.Roughness = 0.72f;
    scene.AddInstance(std::make_shared<MeshData>(primitives::MakePlane(24.0f, 8)),
                      floor, glm::mat4(1.0f));
    scene.Instances().back().SourceEntity = 1;

    const auto cube = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    const auto sphere = std::make_shared<MeshData>(primitives::MakeSphere(1.0f, 32, 32));
    for (int index = 0; index < 5; ++index)
    {
        Material material;
        material.Albedo = glm::mix(glm::vec3(0.08f, 0.32f, 0.72f),
                                   glm::vec3(0.9f, 0.2f, 0.08f),
                                   static_cast<float>(index) / 4.0f);
        material.Metallic = static_cast<float>(index) / 4.0f;
        material.Roughness = 0.12f + static_cast<float>(index) * 0.18f;
        const glm::vec3 position{-5.0f + static_cast<float>(index) * 2.5f,
                                 index % 2 == 0 ? 1.0f : 1.25f, 0.0f};
        scene.AddInstance(index % 2 == 0 ? sphere : cube, material,
            glm::translate(glm::mat4(1.0f), position));
        scene.Instances().back().SourceEntity = static_cast<uint64_t>(100 + index);
    }

    PointLight warm;
    warm.Position = {-3.0f, 4.5f, 3.0f};
    warm.Color = {1.0f, 0.35f, 0.12f};
    warm.Intensity = 75.0f;
    warm.Radius = 16.0f;
    warm.CastsShadows = true;
    scene.AddPointLight(warm);
    PointLight cool = warm;
    cool.Position = {4.0f, 3.5f, -2.0f};
    cool.Color = {0.12f, 0.42f, 1.0f};
    cool.Intensity = 55.0f;
    cool.CastsShadows = false;
    scene.AddPointLight(cool);

    scene.DebugDraw().Grid(12.0f, 1.0f, {0.0f, 0.01f, 0.0f});
    scene.DebugDraw().Icon(DebugIconType::PointLight, warm.Position, warm.Color, 0.45f);
    scene.DebugDraw().Icon(DebugIconType::PointLight, cool.Position, cool.Color, 0.45f);
}

struct ViewState
{
    RenderViewportHandle Handle;
    Camera CameraData;
    uint32_t Width = 1;
    uint32_t Height = 1;
    const char* Name = "Viewport";
};

void DrawView(Application& app, editor::ImGuiLayer& ui, ViewState& view,
              float timeSeconds)
{
    if (!ImGui::Begin(view.Name))
    {
        ImGui::End();
        return;
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const uint32_t width = static_cast<uint32_t>(std::max(available.x, 1.0f));
    const uint32_t height = static_cast<uint32_t>(std::max(available.y, 1.0f));
    if (width != view.Width || height != view.Height)
    {
        if (app.GetSceneRenderer().ResizeViewport(app.GetBackend(), view.Handle, width, height))
        {
            view.Width = width;
            view.Height = height;
        }
    }

    app.GetSceneRenderer().RenderViewport(app.GetBackend(), view.Handle,
        app.GetScene(), view.CameraData, view.Width, view.Height, timeSeconds);
    const RenderTextureHandle texture = app.GetBackend().GetViewportTexture(view.Handle);
    const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
    ui.DrawViewportImage(texture,
        ImVec2(static_cast<float>(view.Width), static_cast<float>(view.Height)));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const PickingResult hit = PickScene(app.GetScene(), view.CameraData,
            mouse.x - imageOrigin.x, mouse.y - imageOrigin.y, view.Width, view.Height);
        ApplyPickingSelection(app.GetScene(), hit);
    }
    ImGui::End();
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        ApplicationDesc description;
        description.Window.title = "Editor foundation - multiple viewports";
        description.Window.width = 1500;
        description.Window.height = 900;
        description.EnableImGui = true;
        description.EnableRuntimeMonitors = false;
        description.Renderer.EnableGpuTiming = true;
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
        PopulateScene(app.GetScene());
        app.GetCamera().Position = {8.0f, 5.0f, 11.0f};
        LookAt(app.GetCamera(), {0.0f, 1.0f, 0.0f});

        ViewState perspective;
        perspective.Name = "Perspective Scene";
        perspective.CameraData = app.GetCamera();
        perspective.Handle = app.GetSceneRenderer().CreateViewport(
            app.GetBackend(), {640, 480, perspective.Name});
        ViewState top;
        top.Name = "Orthographic Top";
        top.CameraData.Position = {0.0f, 18.0f, 0.01f};
        top.CameraData.Yaw = -90.0f;
        top.CameraData.Pitch = -89.9f;
        top.CameraData.Projection = CameraProjection::Orthographic;
        top.CameraData.OrthographicSize = 16.0f;
        top.Handle = app.GetSceneRenderer().CreateViewport(
            app.GetBackend(), {480, 480, top.Name});
        if (!perspective.Handle || !top.Handle)
            throw std::runtime_error("Could not create editor render viewports");

        float elapsed = 0.0f;
        int frameCount = 0;
        bool animateLights = true;
        app.SetUpdateCallback([&](float deltaSeconds) {
            elapsed += deltaSeconds;
            if (animateLights && app.GetScene().PointLights().size() >= 2)
            {
                app.GetScene().PointLights()[0].Position.x = -3.0f + std::sin(elapsed) * 2.0f;
                app.GetScene().PointLights()[1].Position.z = -2.0f + std::cos(elapsed * 0.8f) * 2.0f;
            }
            if (maximumFrames > 0 && ++frameCount >= maximumFrames)
                app.RequestQuit();
        });
        app.SetImGuiCallback([&] {
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::Begin("Viewport controls");
            ImGui::Text("Backend: %s", app.GetBackend().Name());
            ImGui::Checkbox("Animate local lights", &animateLights);
            ImGui::ColorEdit3("Selection color", &app.GetScene().SelectionColor.x,
                              ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
            ImGui::Text("Selected entity: %llu",
                static_cast<unsigned long long>(app.GetScene().SelectedEntity));
            ImGui::Separator();
            ImGui::TextWrapped("Resize or dock both panels. Click geometry in either view to test picking.");
            ImGui::End();
            DrawView(app, *ui, perspective, elapsed);
            DrawView(app, *ui, top, elapsed);
        });

        app.Run();
        app.GetBackend().WaitIdle();
        ui->ForgetTexture(perspective.Handle);
        ui->ForgetTexture(top.Handle);
        app.GetSceneRenderer().DestroyViewport(app.GetBackend(), top.Handle);
        app.GetSceneRenderer().DestroyViewport(app.GetBackend(), perspective.Handle);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "%s\n", exception.what());
        return 1;
    }
}
