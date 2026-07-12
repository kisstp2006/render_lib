#include "SandboxControls.h"

#include "engine/core/Application.h"
#include "engine/core/Log.h"

#include <cmath>
#include <filesystem>
#include <utility>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

using namespace engine;

SandboxControls::SandboxControls(Application& app, bool flashlightOn, bool manageFlashlight, bool localLightShowcase,
                                 bool dayNightShowcase, float dayNightCycleSeconds,
                                 float sunAzimuthDegrees, float sunElevationDegrees,
                                 std::string screenshotPath, int screenshotFrame)
    : App(app), FlashlightOn(flashlightOn), ManageFlashlight(manageFlashlight), LocalLightShowcase(localLightShowcase),
      DayNightShowcase(dayNightShowcase),
      ScreenshotPath(std::move(screenshotPath)), ScreenshotFrame(screenshotFrame),
      SunAzimuth(glm::radians(sunAzimuthDegrees)), SunElevation(glm::radians(sunElevationDegrees)),
      DayNightBaseAzimuth(glm::radians(sunAzimuthDegrees)), DayNightCycleSeconds(glm::max(dayNightCycleSeconds, 4.0f))
{
}

void SandboxControls::Update(float deltaTime)
{
    const Input& input = App.GetInput();
    Scene& scene = App.GetScene();
    Camera& camera = App.GetCamera();

    if (ManageFlashlight && !scene.SpotLights().empty())
    {
        const bool fDown = input.IsKeyDown(GLFW_KEY_F);
        if (fDown && !FWasDown)
            FlashlightOn = !FlashlightOn;
        FWasDown = fDown;

        SpotLight& flashlight = scene.SpotLights()[0];
        flashlight.Enabled = FlashlightOn;
        flashlight.Position = camera.Position;
        flashlight.Direction = camera.Forward();
    }

    const bool cDown = input.IsKeyDown(GLFW_KEY_C);
    if (cDown && !CWasDown)
        scene.Shadows.DebugCascades = !scene.Shadows.DebugCascades;
    CWasDown = cDown;

    if (scene.Environment.Hdri)
    {
        const bool hDown = input.IsKeyDown(GLFW_KEY_H);
        if (hDown && !HWasDown)
        {
            scene.Environment.Source = scene.Environment.Source == EnvironmentSource::EquirectangularHdr
                ? EnvironmentSource::ProceduralSky : EnvironmentSource::EquirectangularHdr;
            log::Info(std::string("Environment source: ")
                      + (scene.Environment.Source == EnvironmentSource::EquirectangularHdr ? "HDRI" : "procedural sky"));
        }
        HWasDown = hDown;

        const bool leftDown = input.IsKeyDown(GLFW_KEY_LEFT_BRACKET);
        const bool rightDown = input.IsKeyDown(GLFW_KEY_RIGHT_BRACKET);
        if (leftDown && !LeftBracketWasDown) scene.Environment.RotationDegrees -= 15.0f;
        if (rightDown && !RightBracketWasDown) scene.Environment.RotationDegrees += 15.0f;
        if ((leftDown && !LeftBracketWasDown) || (rightDown && !RightBracketWasDown))
            log::Info("HDRI rotation: " + std::to_string(scene.Environment.RotationDegrees) + " degrees");
        LeftBracketWasDown = leftDown;
        RightBracketWasDown = rightDown;

        const bool minusDown = input.IsKeyDown(GLFW_KEY_MINUS);
        const bool equalDown = input.IsKeyDown(GLFW_KEY_EQUAL);
        if (minusDown && !MinusWasDown) scene.Environment.ExposureEV -= 0.25f;
        if (equalDown && !EqualWasDown) scene.Environment.ExposureEV += 0.25f;
        if ((minusDown && !MinusWasDown) || (equalDown && !EqualWasDown))
            log::Info("HDRI IBL exposure: " + std::to_string(scene.Environment.ExposureEV) + " EV");
        MinusWasDown = minusDown;
        EqualWasDown = equalDown;
    }

    if (LocalLightShowcase)
    {
        const bool tDown = input.IsKeyDown(GLFW_KEY_T);
        if (tDown && !TWasDown)
        {
            ShowcaseRightShadows = !ShowcaseRightShadows;
            for (PointLight& light : scene.PointLights())
                if (light.Position.x > 0.0f) light.CastsShadows = ShowcaseRightShadows;
            for (SpotLight& light : scene.SpotLights())
                if (light.Position.x > 0.0f) light.CastsShadows = ShowcaseRightShadows;
            for (AreaLight& light : scene.AreaLights())
                if (light.Position.x > 0.0f) light.CastsShadows = ShowcaseRightShadows;
            log::Info(std::string("Showcase right-side local-light shadows: ")
                      + (ShowcaseRightShadows ? "ON" : "OFF"));
        }
        TWasDown = tDown;
    }

    if (DayNightShowcase)
    {
        const bool bDown = input.IsKeyDown(GLFW_KEY_B);
        if (bDown && !BWasDown)
        {
            scene.Sky.StarsEnabled = !scene.Sky.StarsEnabled;
            log::Info(std::string("Stars: ") + (scene.Sky.StarsEnabled ? "ON" : "OFF"));
        }
        BWasDown = bDown;

        const bool nDown = input.IsKeyDown(GLFW_KEY_N);
        if (nDown && !NWasDown)
        {
            scene.Sky.MilkyWayEnabled = !scene.Sky.MilkyWayEnabled;
            log::Info(std::string("Milky Way: ") + (scene.Sky.MilkyWayEnabled ? "ON" : "OFF"));
        }
        NWasDown = nDown;

        const bool mDown = input.IsKeyDown(GLFW_KEY_M);
        if (mDown && !MWasDown)
        {
            // Fold the shader's time-based rotation into the static offset when
            // pausing (and remove it again when resuming), so the sky freezes
            // exactly where it is instead of snapping to another orientation.
            const float animatedRotation = static_cast<float>(glfwGetTime()) * scene.Sky.NightSkyRotationSpeed;
            scene.Sky.NightSkyRotationDegrees += scene.Sky.AnimateNightSky
                ? animatedRotation : -animatedRotation;
            scene.Sky.AnimateNightSky = !scene.Sky.AnimateNightSky;
            log::Info(std::string("Night-sky animation: ") + (scene.Sky.AnimateNightSky ? "ON" : "OFF"));
        }
        MWasDown = mDown;

        const bool rDown = input.IsKeyDown(GLFW_KEY_R);
        if (rDown && !RWasDown)
        {
            NightColorPreset = (NightColorPreset + 1) % 4;
            constexpr const char* names[] = {"NATURAL", "COOL", "WARM", "FANTASY"};
            if (NightColorPreset == 0)
            {
                scene.Sky.StarWarmColor = {1.0f, 0.68f, 0.46f};
                scene.Sky.StarCoolColor = {0.62f, 0.78f, 1.0f};
                scene.Sky.MilkyWayColor = {0.22f, 0.30f, 0.65f};
            }
            else if (NightColorPreset == 1)
            {
                scene.Sky.StarWarmColor = {0.72f, 0.82f, 1.0f};
                scene.Sky.StarCoolColor = {0.38f, 0.62f, 1.0f};
                scene.Sky.MilkyWayColor = {0.16f, 0.30f, 0.95f};
            }
            else if (NightColorPreset == 2)
            {
                scene.Sky.StarWarmColor = {1.0f, 0.48f, 0.20f};
                scene.Sky.StarCoolColor = {1.0f, 0.86f, 0.62f};
                scene.Sky.MilkyWayColor = {0.72f, 0.24f, 0.18f};
            }
            else
            {
                scene.Sky.StarWarmColor = {1.0f, 0.42f, 0.86f};
                scene.Sky.StarCoolColor = {0.38f, 0.88f, 1.0f};
                scene.Sky.MilkyWayColor = {0.64f, 0.18f, 1.0f};
            }
            log::Info(std::string("Night color preset: ") + names[NightColorPreset]);
        }
        RWasDown = rDown;

        const bool upDown = input.IsKeyDown(GLFW_KEY_UP);
        const bool downDown = input.IsKeyDown(GLFW_KEY_DOWN);
        const bool leftDown = input.IsKeyDown(GLFW_KEY_LEFT);
        const bool rightDown = input.IsKeyDown(GLFW_KEY_RIGHT);
        if (upDown && !UpWasDown) scene.Sky.StarDensity = glm::min(scene.Sky.StarDensity + 0.001f, 0.05f);
        if (downDown && !DownWasDown) scene.Sky.StarDensity = glm::max(scene.Sky.StarDensity - 0.001f, 0.0f);
        if (rightDown && !RightWasDown) scene.Sky.StarIntensity += 0.5f;
        if (leftDown && !LeftWasDown) scene.Sky.StarIntensity = glm::max(scene.Sky.StarIntensity - 0.5f, 0.0f);
        if ((upDown && !UpWasDown) || (downDown && !DownWasDown))
            log::Info("Star density: " + std::to_string(scene.Sky.StarDensity));
        if ((leftDown && !LeftWasDown) || (rightDown && !RightWasDown))
            log::Info("Star intensity: " + std::to_string(scene.Sky.StarIntensity));
        UpWasDown = upDown;
        DownWasDown = downDown;
        LeftWasDown = leftDown;
        RightWasDown = rightDown;

        const bool pDown = input.IsKeyDown(GLFW_KEY_P);
        if (pDown && !PWasDown)
        {
            DayNightPaused = !DayNightPaused;
            log::Info(std::string("Day/night cycle: ") + (DayNightPaused ? "PAUSED" : "RUNNING"));
        }
        PWasDown = pDown;

        const bool commaDown = input.IsKeyDown(GLFW_KEY_COMMA);
        const bool periodDown = input.IsKeyDown(GLFW_KEY_PERIOD);
        if (commaDown && !CommaWasDown) DayNightSpeed = glm::max(DayNightSpeed * 0.5f, 0.125f);
        if (periodDown && !PeriodWasDown) DayNightSpeed = glm::min(DayNightSpeed * 2.0f, 8.0f);
        if ((commaDown && !CommaWasDown) || (periodDown && !PeriodWasDown))
            log::Info("Day/night speed: " + std::to_string(DayNightSpeed) + "x");
        CommaWasDown = commaDown;
        PeriodWasDown = periodDown;

        if (!DayNightPaused)
            DayNightPhase += deltaTime * DayNightSpeed * glm::two_pi<float>() / DayNightCycleSeconds;

        // A smooth solar arc from +35 to -22 degrees. A small azimuth drift
        // makes the directional-light rotation obvious without moving the sun
        // out of the showcase camera's useful hemisphere.
        const float elevationDegrees = 6.5f + 28.5f * std::sin(DayNightPhase);
        SunElevation = glm::radians(elevationDegrees);
        SunAzimuth = DayNightBaseAzimuth + glm::radians(12.0f) * std::cos(DayNightPhase);

        const int stage = elevationDegrees > 3.0f ? 0 : elevationDegrees > -6.0f ? 1
                        : elevationDegrees > -18.0f ? 2 : 3;
        if (stage != LastDayNightStage)
        {
            constexpr const char* names[] = {"DAY", "SUNSET", "TWILIGHT", "NIGHT"};
            log::Info(std::string("Day/night stage: ") + names[stage]);
            LastDayNightStage = stage;
        }
    }
    else
    {
        const float rotationSpeed = 0.8f * deltaTime;
        if (input.IsKeyDown(GLFW_KEY_J)) SunAzimuth -= rotationSpeed;
        if (input.IsKeyDown(GLFW_KEY_L)) SunAzimuth += rotationSpeed;
        if (input.IsKeyDown(GLFW_KEY_I)) SunElevation = glm::min(SunElevation + rotationSpeed, glm::radians(89.0f));
        if (input.IsKeyDown(GLFW_KEY_K)) SunElevation = glm::max(SunElevation - rotationSpeed, glm::radians(-30.0f));
    }

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
