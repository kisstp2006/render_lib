#pragma once

#include <string>

namespace engine {
class Application;
}

class SandboxControls
{
public:
    SandboxControls(engine::Application& app, bool flashlightOn, std::string screenshotPath, int screenshotFrame);
    void Update(float deltaTime);

private:
    engine::Application& App;
    bool FlashlightOn = false;
    std::string ScreenshotPath;
    int ScreenshotFrame = 10;
    int FrameCounter = 0;
    int ManualShotCounter = 0;
    bool F12WasDown = false;
    bool FWasDown = false;
    bool CWasDown = false;
    float SunAzimuth;
    float SunElevation;
};
