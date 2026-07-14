#pragma once

enum class SandboxPreset
{
    Generic,
    Materials,
    Gltf,
    Lights,
    HdriStudio,
    DayNight,
    PostProcessing,
    Stability,
    Visibility
};

// Shared application entry point used by the generic sandbox and every
// standalone sample executable. Command-line options can still override the
// selected preset, without duplicating setup or event-loop code.
int RunSandboxApp(int argc, char** argv, SandboxPreset preset);
