#include "SandboxControls.h"

#include "engine/core/Application.h"

#include <cmath>
#include <filesystem>
#include <utility>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

using namespace engine;

SandboxControls::SandboxControls(Application& app, bool flashlightOn, std::string screenshotPath, int screenshotFrame)
    : App(app), FlashlightOn(flashlightOn), ScreenshotPath(std::move(screenshotPath)), ScreenshotFrame(screenshotFrame),
      SunAzimuth(glm::radians(215.0f)), SunElevation(glm::radians(40.0f))
{
}

void SandboxControls::Update(float deltaTime)
{
    const Input& input = App.GetInput();
    Scene& scene = App.GetScene();
    Camera& camera = App.GetCamera();

    const bool fDown = input.IsKeyDown(GLFW_KEY_F);
    if (fDown && !FWasDown)
        FlashlightOn = !FlashlightOn;
    FWasDown = fDown;

    const bool cDown = input.IsKeyDown(GLFW_KEY_C);
    if (cDown && !CWasDown)
        scene.Shadows.DebugCascades = !scene.Shadows.DebugCascades;
    CWasDown = cDown;

    SpotLight& flashlight = scene.SpotLights()[0];
    flashlight.Enabled = FlashlightOn;
    flashlight.Position = camera.Position;
    flashlight.Direction = camera.Forward();

    const float rotationSpeed = 0.8f * deltaTime;
    if (input.IsKeyDown(GLFW_KEY_J)) SunAzimuth -= rotationSpeed;
    if (input.IsKeyDown(GLFW_KEY_L)) SunAzimuth += rotationSpeed;
    if (input.IsKeyDown(GLFW_KEY_I)) SunElevation = glm::min(SunElevation + rotationSpeed, glm::radians(89.0f));
    if (input.IsKeyDown(GLFW_KEY_K)) SunElevation = glm::max(SunElevation - rotationSpeed, glm::radians(-5.0f));

    const glm::vec3 toSun{
        std::cos(SunElevation) * std::cos(SunAzimuth),
        std::sin(SunElevation),
        std::cos(SunElevation) * std::sin(SunAzimuth)};
    scene.Sun.Direction = -toSun;

    const bool f12Down = input.IsKeyDown(GLFW_KEY_F12);
    if (f12Down && !F12WasDown)
    {
        std::filesystem::create_directories("renders");
        App.GetBackend().RequestScreenshot("renders/render_" + std::to_string(ManualShotCounter++) + ".png");
    }
    F12WasDown = f12Down;

    if (!ScreenshotPath.empty())
    {
        ++FrameCounter;
        if (FrameCounter == ScreenshotFrame)
            App.GetBackend().RequestScreenshot(ScreenshotPath);
        else if (FrameCounter > ScreenshotFrame + 1)
            App.GetWindow().RequestClose();
    }
}
