// Sandbox: a grid of spheres sweeping metallic (rows) x roughness (columns),
// a textured ground plane, procedural sky with IBL, sun + two point lights -
// the standard PBR material-response demo scene with the Source 2-style
// HDR/bloom/tonemap pipeline on top.
//
// Usage: sandbox [--vulkan] [--screenshot <path.png> [--frames N]]
//
// Controls:
//   RMB + mouse   look around          WASD/Q/E  move (Shift = faster)
//   I/K           sun elevation        J/L       sun azimuth
//   F12           save render to renders/render_<n>.png

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/core/Application.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Texture.h"

using namespace engine;

int main(int argc, char** argv)
{
    WindowDesc desc;
    desc.title = "Source-Like PBR Sandbox";
    desc.width = 1600;
    desc.height = 900;
    desc.api = GraphicsApi::OpenGL;

    std::string screenshotPath;
    int screenshotFrame = 10;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--vulkan")
            desc.api = GraphicsApi::Vulkan;
        else if (arg == "--screenshot" && i + 1 < argc)
            screenshotPath = argv[++i];
        else if (arg == "--frames" && i + 1 < argc)
            screenshotFrame = std::atoi(argv[++i]);
    }

    Application app(desc);
    Scene& scene = app.GetScene();

    scene.Sun.Direction = glm::normalize(glm::vec3(-0.35f, -0.65f, -0.25f));
    scene.Sun.Color = {1.0f, 0.95f, 0.85f};
    scene.Sun.Intensity = 7.0f;
    scene.Sky.SkyIntensity = 0.45f;   // keep IBL ambient well below the sun
    scene.PostProcess.Exposure = 1.7f;

    auto sphereMesh = std::make_shared<MeshData>(primitives::MakeSphere(0.9f, 48, 48));
    auto planeMesh = std::make_shared<MeshData>(primitives::MakePlane(40.0f, 1));

    // Ground: subtle checker so the texture path and shadows both read well.
    Material groundMat;
    groundMat.Albedo = {1.0f, 1.0f, 1.0f};
    groundMat.Metallic = 0.0f;
    groundMat.Roughness = 0.8f;
    // Plane UVs span 0..40 (1 repeat per meter); 2 cells/repeat = 0.5 m checker
    groundMat.AlbedoMap = textures::MakeChecker(256, 2, {0.30f, 0.30f, 0.32f}, {0.38f, 0.38f, 0.40f});
    scene.AddInstance(planeMesh, groundMat, glm::translate(glm::mat4(1.0f), {0.0f, -1.0f, 0.0f}));

    constexpr int kRows = 5;    // metallic 0 -> 1
    constexpr int kCols = 7;    // roughness 0.05 -> 1
    constexpr float kSpacing = 2.2f;

    for (int row = 0; row < kRows; ++row)
    {
        for (int col = 0; col < kCols; ++col)
        {
            Material mat;
            mat.Albedo = {0.92f, 0.2f, 0.15f}; // consistent base color so metal/rough response is easy to read
            mat.Metallic = static_cast<float>(row) / static_cast<float>(kRows - 1);
            mat.Roughness = glm::mix(0.05f, 1.0f, static_cast<float>(col) / static_cast<float>(kCols - 1));

            const float x = (col - (kCols - 1) * 0.5f) * kSpacing;
            const float y = (kRows - 1 - row) * kSpacing * 0.5f + 0.4f;
            const glm::mat4 transform = glm::translate(glm::mat4(1.0f), {x, y, 0.0f});

            scene.AddInstance(sphereMesh, mat, transform);
        }
    }

    // One emissive sphere so bloom has an in-scene source besides the sun.
    Material glowMat;
    glowMat.Albedo = {0.1f, 0.1f, 0.1f};
    glowMat.Roughness = 0.6f;
    glowMat.Emissive = {4.0f, 1.6f, 0.4f};
    scene.AddInstance(sphereMesh, glowMat, glm::translate(glm::mat4(1.0f), {-9.5f, 0.2f, 2.5f}));

    PointLight fill;
    fill.Position = {-6.0f, 5.0f, 6.0f};
    fill.Color = {0.4f, 0.55f, 1.0f};
    fill.Intensity = 40.0f;
    fill.Radius = 20.0f;
    scene.AddPointLight(fill);

    PointLight rim;
    rim.Position = {8.0f, 3.0f, -4.0f};
    rim.Color = {1.0f, 0.6f, 0.3f};
    rim.Intensity = 30.0f;
    rim.Radius = 18.0f;
    scene.AddPointLight(rim);

    Camera& camera = app.GetCamera();
    camera.Position = {0.0f, 3.5f, 12.0f};
    camera.Yaw = -90.0f;
    camera.Pitch = -12.0f;

    // Sun driven in spherical coordinates by I/K/J/L
    float sunAzimuth = glm::radians(215.0f);
    float sunElevation = glm::radians(40.0f);

    int frameCounter = 0;
    int manualShotCounter = 0;
    bool f12WasDown = false;

    app.SetUpdateCallback([&](float dt) {
        const Input& input = app.GetInput();

        const float rotSpeed = 0.8f * dt;
        if (input.IsKeyDown(GLFW_KEY_J)) sunAzimuth -= rotSpeed;
        if (input.IsKeyDown(GLFW_KEY_L)) sunAzimuth += rotSpeed;
        if (input.IsKeyDown(GLFW_KEY_I)) sunElevation = glm::min(sunElevation + rotSpeed, glm::radians(89.0f));
        if (input.IsKeyDown(GLFW_KEY_K)) sunElevation = glm::max(sunElevation - rotSpeed, glm::radians(-5.0f));

        const glm::vec3 toSun{
            std::cos(sunElevation) * std::cos(sunAzimuth),
            std::sin(sunElevation),
            std::cos(sunElevation) * std::sin(sunAzimuth)};
        scene.Sun.Direction = -toSun;

        // F12: save a render (edge-triggered)
        const bool f12Down = input.IsKeyDown(GLFW_KEY_F12);
        if (f12Down && !f12WasDown)
        {
            std::filesystem::create_directories("renders");
            app.GetBackend().RequestScreenshot("renders/render_" + std::to_string(manualShotCounter++) + ".png");
        }
        f12WasDown = f12Down;

        // Headless-ish capture mode: render N frames, save, quit.
        if (!screenshotPath.empty())
        {
            ++frameCounter;
            if (frameCounter == screenshotFrame)
                app.GetBackend().RequestScreenshot(screenshotPath);
            else if (frameCounter > screenshotFrame + 1)
                app.GetWindow().RequestClose();
        }
    });

    app.Run();
    return 0;
}
