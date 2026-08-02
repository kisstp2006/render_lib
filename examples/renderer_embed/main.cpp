#include "engine/renderer/Renderer.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    try
    {
        rendering::RendererDesc desc;
        desc.WindowTitle = "Rendering Engine - C++ SDK Example";
        desc.Width = 960;
        desc.Height = 540;
        desc.ShaderDirectory = std::filesystem::absolute(argv[0]).parent_path() / "shaders";
        uint32_t frameCount = 0;
        std::filesystem::path screenshot;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);
            if (argument == "--vulkan") desc.GraphicsBackend = rendering::Backend::Vulkan;
            else if (argument == "--opengl") desc.GraphicsBackend = rendering::Backend::OpenGL;
            else if (argument == "--hidden") desc.Visible = false;
            else if (argument == "--validation") desc.Validation = true;
            else if (argument == "--frames" && index + 1 < argc)
                frameCount = static_cast<uint32_t>(std::strtoul(argv[++index], nullptr, 10));
            else if (argument == "--screenshot" && index + 1 < argc)
                screenshot = argv[++index];
        }

        auto renderer = rendering::Renderer::Create(desc);
        rendering::CameraDesc camera;
        camera.Position[1] = 2.4f;
        camera.Position[2] = 8.0f;
        camera.PitchDegrees = -12.0f;
        renderer->SetCamera(camera);

        rendering::MaterialDesc metal;
        metal.Albedo[0] = 0.42f;
        metal.Albedo[1] = 0.12f;
        metal.Albedo[2] = 0.06f;
        metal.Metallic = 0.85f;
        metal.Roughness = 0.22f;
        const auto metalMaterial = renderer->CreateMaterial(metal);

        rendering::MaterialDesc floor;
        floor.Albedo[0] = floor.Albedo[1] = floor.Albedo[2] = 0.28f;
        floor.Roughness = 0.82f;
        const auto floorMaterial = renderer->CreateMaterial(floor);
        const auto sphere = renderer->CreateSphere(1.0f, 32, 32);
        const auto cube = renderer->CreateCube(1.0f);
        const auto sphereObject = renderer->AddObject(sphere, metalMaterial,
            rendering::ComposeTransform({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
                                        {1.0f, 1.0f, 1.0f}));
        renderer->AddObject(cube, floorMaterial,
            rendering::ComposeTransform({0.0f, -0.25f, 0.0f}, {0.0f, 0.0f, 0.0f},
                                        {5.0f, 0.2f, 5.0f}));

        rendering::PointLightDesc light;
        light.Position[0] = -2.5f;
        light.Position[1] = 3.5f;
        light.Position[2] = 2.0f;
        light.Color[0] = 1.0f;
        light.Color[1] = 0.42f;
        light.Color[2] = 0.16f;
        light.Intensity = 80.0f;
        light.Radius = 12.0f;
        renderer->AddPointLight(light);

        uint32_t frame = 0;
        while ((frameCount == 0 || frame < frameCount) && renderer->PumpEvents())
        {
            const float angle = static_cast<float>(frame) * 0.8f;
            renderer->SetObjectTransform(sphereObject,
                rendering::ComposeTransform({0.0f, 1.0f, 0.0f}, {0.0f, angle, 0.0f},
                                            {1.0f, 1.0f, 1.0f}));
            if (!screenshot.empty() && frame + 1 == std::max(frameCount, 1u))
                renderer->RequestScreenshot(screenshot);
            renderer->RenderFrame(1.0f / 60.0f);
            ++frame;
        }
        std::cout << renderer->BackendName() << " SDK example completed " << frame
                  << " frame(s)\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
