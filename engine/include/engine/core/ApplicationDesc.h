#pragma once

#include "engine/backend/IRenderBackend.h"
#include "engine/core/Window.h"

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

    UnfocusedBehavior Unfocused = UnfocusedBehavior::Continue;
    float MaximumDeltaSeconds = 0.1f;
    double FrameRateLimit = 0.0; // Zero disables the software limiter.
    bool CaptureCursorOnRightMouse = true;
};

} // namespace engine
