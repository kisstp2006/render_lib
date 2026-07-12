// Sandbox: a grid of spheres sweeping metallic (rows) x roughness (columns),
// a ground plane, one directional "sun" and a couple of point lights - the
// standard PBR material-response demo scene, lit/shadowed Source2-style.
//
// Usage: sandbox [--vulkan]   (defaults to the OpenGL backend)

#include <memory>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/core/Application.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Scene.h"

using namespace engine;

int main(int argc, char** argv)
{
    WindowDesc desc;
    desc.title = "Source-Like PBR Sandbox";
    desc.width = 1600;
    desc.height = 900;
    desc.api = GraphicsApi::OpenGL;

    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--vulkan")
            desc.api = GraphicsApi::Vulkan;
    }

    Application app(desc);
    Scene& scene = app.GetScene();

    scene.Sun.Direction = glm::normalize(glm::vec3(-0.35f, -0.9f, -0.25f));
    scene.Sun.Color = {1.0f, 0.95f, 0.85f};
    scene.Sun.Intensity = 3.2f;
    scene.AmbientColor = {0.30f, 0.34f, 0.42f};

    auto sphereMesh = std::make_shared<MeshData>(primitives::MakeSphere(0.9f, 48, 48));
    auto planeMesh = std::make_shared<MeshData>(primitives::MakePlane(40.0f, 1));

    // Ground plane: matte dielectric, similar to a Source 2 concrete/plaster material.
    Material groundMat;
    groundMat.Albedo = {0.32f, 0.32f, 0.34f};
    groundMat.Metallic = 0.0f;
    groundMat.Roughness = 0.85f;
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

    app.Run();
    return 0;
}
