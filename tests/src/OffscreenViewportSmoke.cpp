#include "engine/backend/gl/GLRenderBackend.h"
#if ENGINE_HAS_VULKAN
#include "engine/backend/vk/VulkanRenderBackend.h"
#endif
#include "engine/core/Camera.h"
#include "engine/core/Window.h"
#if ENGINE_ENABLE_IMGUI
#include "engine/editor/ImGuiLayer.h"
#endif
#include "engine/render/SceneRenderer.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Scene.h"

#include <cstdio>
#include <memory>
#include <string_view>

#include <glm/gtc/matrix_transform.hpp>

using namespace engine;

int main(int argc, char** argv)
{
    const bool vulkan = argc > 1 && std::string_view(argv[1]) == "--vulkan";
    WindowDesc windowDesc;
    windowDesc.title = "Offscreen viewport smoke";
    windowDesc.width = 640;
    windowDesc.height = 360;
    windowDesc.visible = false;
    windowDesc.api = vulkan ? GraphicsApi::Vulkan : GraphicsApi::OpenGL;
    Window window(windowDesc);

    std::unique_ptr<IRenderBackend> backend;
    if (vulkan)
    {
#if ENGINE_HAS_VULKAN
        backend = std::make_unique<VulkanRenderBackend>();
#else
        return 77;
#endif
    }
    else
        backend = std::make_unique<GLRenderBackend>();
    RenderBackendConfig config;
    config.EnableValidation = true;
    config.EnableGpuTiming = false;
    backend->Init(window, config);

    Scene scene;
    scene.PostProcess.AutoExposure = false;
    scene.PostProcess.AntiAliasing = AntiAliasingMode::None;
    scene.Visibility.GpuOcclusionCulling = false;
    scene.Batching.MinimumDynamicBatchSize = 2;
    scene.Batching.PreferInstancingForRepeatedMeshes = false;
    const auto cube = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    for (int index = 0; index < 2; ++index)
    {
        scene.AddInstance(cube, Material{}, glm::translate(
            glm::mat4(1.0f), {index == 0 ? -1.1f : 1.1f, 0.0f, 0.0f}));
        scene.Instances().back().Mobility = MeshMobility::Movable;
        scene.Instances().back().TemporalId = static_cast<uint64_t>(index + 1);
    }
    Camera camera;
    camera.Position = {0.0f, 0.0f, 5.0f};
    camera.Yaw = -90.0f;
    camera.Pitch = 0.0f;
    SceneRenderer renderer;

    const RenderViewportHandle first = backend->CreateViewport({320, 180, "Scene A"});
    const RenderViewportHandle second = backend->CreateViewport({128, 128, "Scene B"});
    const RenderViewportHandle clamped = backend->CreateViewport({0, 0, "Clamped"});
    if (!first || !second || first == second)
        return 2;
    const RenderTextureHandle clampedTexture = backend->GetViewportTexture(clamped);
    if (!clamped || clampedTexture.Width != 1 || clampedTexture.Height != 1)
        return 9;
    backend->DestroyViewport(clamped);
    if (backend->GetViewportTexture(clamped) ||
        renderer.RenderViewport(*backend, clamped, scene, camera, 1, 1))
        return 10;
    if (!renderer.RenderViewport(*backend, first, scene, camera, 320, 180) ||
        !renderer.RenderViewport(*backend, second, scene, camera, 128, 128))
        return 3;
    scene.Instances()[0].Transform[3].y += 0.35f;
    if (!renderer.RenderViewport(*backend, first, scene, camera, 320, 180))
        return 17;
    const RenderTextureHandle firstTexture = backend->GetViewportTexture(first);
    const RenderTextureHandle secondTexture = backend->GetViewportTexture(second);
    if (!firstTexture || !secondTexture || firstTexture.Width != 320 ||
        secondTexture.Height != 128)
        return 4;
    if (firstTexture.Api != secondTexture.Api ||
        (firstTexture.NativeTexture == secondTexture.NativeTexture &&
         firstTexture.NativeImageView == secondTexture.NativeImageView))
        return 11;
    if (!backend->ResizeViewport(first, 320, 180) ||
        backend->GetViewportTexture(first).Generation != firstTexture.Generation ||
        backend->ResizeViewport(first, 0, 180) ||
        backend->GetViewportTexture(first).Generation != firstTexture.Generation)
        return 12;
    if (!backend->ResizeViewport(first, 256, 144))
        return 5;
    const RenderTextureHandle resized = backend->GetViewportTexture(first);
    if (!resized || resized.Width != 256 || resized.Height != 144 ||
        resized.Generation <= firstTexture.Generation)
        return 6;
    if (renderer.RenderViewport(*backend, first, scene, camera, 320, 180))
        return 13;
    if (!renderer.RenderViewport(*backend, first, scene, camera, 256, 144))
        return 7;
    const RenderTextureHandle firstAfterSecondRender = backend->GetViewportTexture(first);
    if (!renderer.RenderViewport(*backend, second, scene, camera, 128, 128) ||
        backend->GetViewportTexture(first).NativeTexture != firstAfterSecondRender.NativeTexture ||
        backend->GetViewportTexture(first).NativeImageView != firstAfterSecondRender.NativeImageView ||
        backend->GetViewportTexture(first).Generation != firstAfterSecondRender.Generation)
        return 14;

#if ENGINE_ENABLE_IMGUI
    editor::ImGuiLayer ui;
    if (!ui.Initialize(window, *backend))
        return 8;
    ui.BeginFrame();
    ImGui::Begin("Viewport smoke");
    ui.DrawViewportImage(backend->GetViewportTexture(first), ImVec2(256, 144));
    ImGui::End();
    RenderTextureHandle wrongApi = backend->GetViewportTexture(first);
    wrongApi.Api = wrongApi.Api == RenderBackendApi::OpenGL
        ? RenderBackendApi::Vulkan : RenderBackendApi::OpenGL;
    if (ui.TextureId(wrongApi) != 0)
        return 15;
    ui.EndFrame();
    const RenderFrameData& mainFrame = renderer.PrepareFrame(scene, camera, 640, 360);
    backend->RenderFrame(mainFrame);
    if (!vulkan)
        window.SwapBuffers();
    backend->WaitIdle();
    ui.ForgetTexture(first);
    ui.Shutdown();
#endif
    backend->DestroyViewport(second);
    if (backend->GetViewportTexture(second) ||
        backend->ResizeViewport(second, 64, 64) ||
        renderer.RenderViewport(*backend, second, scene, camera, 128, 128))
        return 16;
    backend->DestroyViewport(first);
    backend->Shutdown();
    std::puts(vulkan ? "Vulkan offscreen viewport smoke passed"
                     : "OpenGL offscreen viewport smoke passed");
    return 0;
}
