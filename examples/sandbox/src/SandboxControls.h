#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace engine {
class Application;
}

class SandboxControls
{
public:
    SandboxControls(engine::Application& app, bool flashlightOn, bool manageFlashlight, bool localLightShowcase,
                    bool dayNightShowcase, bool postShowcase, float dayNightCycleSeconds,
                    float sunAzimuthDegrees, float sunElevationDegrees,
                    std::string screenshotPath, int screenshotFrame);
    void Update(float deltaTime);

private:
    engine::Application& App;
    bool FlashlightOn = false;
    bool ManageFlashlight = true;
    bool LocalLightShowcase = false;
    bool DayNightShowcase = false;
    bool PostShowcase = false;
    bool DayNightPaused = false;
    bool ShowcaseRightShadows = false;
    std::string ScreenshotPath;
    int ScreenshotFrame = 10;
    int FrameCounter = 0;
    int ManualShotCounter = 0;
    bool F12WasDown = false;
    bool FWasDown = false;
    bool CWasDown = false;
    bool TWasDown = false;
    bool HWasDown = false;
    bool LeftBracketWasDown = false;
    bool RightBracketWasDown = false;
    bool MinusWasDown = false;
    bool EqualWasDown = false;
    bool PWasDown = false;
    bool CommaWasDown = false;
    bool PeriodWasDown = false;
    bool BWasDown = false;
    bool NWasDown = false;
    bool MWasDown = false;
    bool RWasDown = false;
    bool UpWasDown = false;
    bool DownWasDown = false;
    bool LeftWasDown = false;
    bool RightWasDown = false;
    bool VWasDown = false;
    bool GWasDown = false;
    bool SemicolonWasDown = false;
    bool ApostropheWasDown = false;
    uint64_t AnimatedInstanceId = 0;
    glm::mat4 AnimatedBaseTransform{1.0f};
    float PostAnimationTime = 0.0f;
    float SavedLutWeight = 1.0f;
    int NightColorPreset = 0;
    int LastDayNightStage = -1;
    float SunAzimuth;
    float SunElevation;
    float DayNightBaseAzimuth;
    float DayNightPhase = 2.8396f;
    float DayNightCycleSeconds = 24.0f;
    float DayNightSpeed = 1.0f;
};
