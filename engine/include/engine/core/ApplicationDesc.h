#pragma once

#include "engine/backend/IRenderBackend.h"
#include "engine/core/Window.h"
#include "engine/debug/RenderDocCapture.h"

#ifndef ENGINE_ENABLE_RUNTIME_MONITORS
#define ENGINE_ENABLE_RUNTIME_MONITORS 1
#endif

namespace engine
{

enum class UnfocusedBehavior
{
    Continue,
    RenderOnly,
    Pause
};

enum class ApplicationEventType
{
    BeginFrame,
    EnteredBackground,
    EnteredForeground,
    BeforeUpdate,
    AfterUpdate,
    BeforeRender,
    AfterRender,
    EndFrame
};

struct ApplicationEvent
{
    ApplicationEventType Type = ApplicationEventType::BeginFrame;
    float DeltaSeconds = 0.0f;
    uint64_t FrameIndex = 0;
};

struct ApplicationDesc
{
    WindowDesc Window;
    RenderBackendConfig Renderer;
    debug::RenderDocCaptureConfig FrameCapture;

    UnfocusedBehavior Unfocused = UnfocusedBehavior::Continue;
    float MaximumDeltaSeconds = 0.1f;
    // Positive values make simulation time deterministic, which is useful for
    // captures and visual regression tests. Zero uses the real frame clock.
    float FixedDeltaSeconds = 0.0f;
    double FrameRateLimit = 0.0; // Zero disables the software limiter.
    bool CaptureCursorOnRightMouse = true;
    // Persistent CPU-memory and GPU cards are enabled for runtime builds by
    // default. Set this to false before constructing Application to disable
    // both collection and rendering for this application.
    bool EnableRuntimeMonitors = ENGINE_ENABLE_RUNTIME_MONITORS != 0;
    // Editor/runtime worlds may drive the renderer Scene through built-in
    // render components. Sandbox-style applications that populate Scene
    // directly keep this disabled.
    bool SynchronizeWorldToScene = false;
    // Editor UI is opt-in so runtime samples keep their zero-UI path.
    bool EnableImGui = false;
    bool EnableImGuiPlatformViewports = false;
    std::string ImGuiIniFilename = ".cache/editor/imgui.ini";
};

} // namespace engine
