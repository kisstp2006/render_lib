#include "engine/core/ApplicationConfig.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace engine
{
namespace
{

std::string Trim(std::string_view text)
{
    const auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!text.empty() && whitespace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && whitespace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string(text);
}

bool ParseInt(std::string_view text, int& result)
{
    const std::string value = Trim(text);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

bool ParseUnsigned(std::string_view text, uint32_t& result)
{
    const std::string value = Trim(text);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

bool ParseDouble(std::string_view text, double& result)
{
    const std::string value = Trim(text);
    char* end = nullptr;
    result = std::strtod(value.c_str(), &end);
    return end == value.c_str() + value.size();
}

bool ParseBool(std::string_view text, bool& result)
{
    std::string value = Trim(text);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "true" || value == "1")
    {
        result = true;
        return true;
    }
    if (value == "false" || value == "0")
    {
        result = false;
        return true;
    }
    return false;
}

bool ParseString(std::string_view text, std::string& result)
{
    const std::string value = Trim(text);
    if (!value.empty() && value.front() == '"')
    {
        std::istringstream stream(value);
        stream >> std::quoted(result);
        return !stream.fail();
    }
    result = value;
    return true;
}

template <typename Enum>
bool ParseEnum(std::string_view text, std::initializer_list<std::pair<std::string_view, Enum>> values,
               Enum& result)
{
    std::string value = Trim(text);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& [name, enumValue] : values)
    {
        if (value == name)
        {
            result = enumValue;
            return true;
        }
    }
    return false;
}

const char* Name(GraphicsApi value) { return value == GraphicsApi::Vulkan ? "vulkan" : "opengl"; }

const char* Name(WindowMode value)
{
    switch (value)
    {
    case WindowMode::WindowedFixed:
        return "windowed_fixed";
    case WindowMode::BorderlessFullscreen:
        return "borderless";
    case WindowMode::ExclusiveFullscreen:
        return "fullscreen";
    default:
        return "windowed_resizable";
    }
}

const char* Name(CursorMode value)
{
    switch (value)
    {
    case CursorMode::Hidden:
        return "hidden";
    case CursorMode::Captured:
        return "captured";
    default:
        return "normal";
    }
}

const char* Name(PresentMode value)
{
    switch (value)
    {
    case PresentMode::Immediate:
        return "immediate";
    case PresentMode::Adaptive:
        return "adaptive";
    default:
        return "vsync";
    }
}

const char* Name(GpuCapabilityPolicy value)
{
    return value == GpuCapabilityPolicy::Conservative ? "conservative" : "default";
}

const char* Name(UnfocusedBehavior value)
{
    switch (value)
    {
    case UnfocusedBehavior::RenderOnly:
        return "render";
    case UnfocusedBehavior::Pause:
        return "pause";
    default:
        return "continue";
    }
}

bool Invalid(std::string* error, size_t line, const std::string& key)
{
    if (error)
        *error = "Invalid value for '" + key + "' on line " + std::to_string(line);
    return false;
}

} // namespace

bool SaveApplicationConfig(const std::filesystem::path& path, const ApplicationDesc& config,
                           std::string* error)
{
    std::error_code directoryError;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError)
    {
        if (error)
            *error = "Cannot create config directory: " + directoryError.message();
        return false;
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file)
    {
        if (error)
            *error = "Cannot open application config for writing: " + path.string();
        return false;
    }
    const WindowDesc& window = config.Window;
    const RenderBackendConfig& renderer = config.Renderer;
    file << "# Source-Like Rendering Engine application config v1\n"
         << "window.title=" << std::quoted(window.title) << '\n'
         << "window.width=" << window.width << '\n'
         << "window.height=" << window.height << '\n'
         << "window.api=" << Name(window.api) << '\n'
         << "window.mode=" << Name(window.mode) << '\n'
         << "window.monitor=" << window.monitor << '\n'
         << "window.position_x=" << window.positionX << '\n'
         << "window.position_y=" << window.positionY << '\n'
         << "window.center=" << std::boolalpha << window.centerOnMonitor << '\n'
         << "window.focus_on_show=" << window.focusOnShow << '\n'
         << "window.visible=" << window.visible << '\n'
         << "window.cursor=" << Name(window.cursor) << '\n'
         << "renderer.present=" << Name(renderer.Presentation) << '\n'
         << "renderer.msaa=" << renderer.MsaaSamples << '\n'
         << "renderer.anisotropy=" << renderer.MaxAnisotropy << '\n'
         << "renderer.prefer_discrete_gpu=" << renderer.PreferDiscreteGpu << '\n'
         << "renderer.preferred_adapter=" << std::quoted(renderer.PreferredAdapter) << '\n'
         << "renderer.validation=" << renderer.EnableValidation << '\n'
         << "renderer.gpu_timing=" << renderer.EnableGpuTiming << '\n'
         << "renderer.gpu_policy=" << Name(renderer.CapabilityPolicy) << '\n'
         << "renderer.driver_workarounds=" << renderer.EnableDriverWorkarounds << '\n'
         << "application.unfocused=" << Name(config.Unfocused) << '\n'
         << "application.max_delta=" << config.MaximumDeltaSeconds << '\n'
         << "application.fixed_delta=" << config.FixedDeltaSeconds << '\n'
         << "application.max_fps=" << config.FrameRateLimit << '\n'
         << "application.capture_cursor_on_look=" << config.CaptureCursorOnRightMouse << '\n'
         << "diagnostics.runtime_monitors=" << config.EnableRuntimeMonitors << '\n';
    if (!file)
    {
        if (error)
            *error = "Failed while writing application config: " + path.string();
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool LoadApplicationConfig(const std::filesystem::path& path, ApplicationDesc& config, std::string* error)
{
    std::ifstream file(path);
    if (!file)
    {
        if (error)
            *error = "Cannot open application config: " + path.string();
        return false;
    }

    ApplicationDesc parsed = config;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == '#')
            continue;
        const size_t separator = trimmed.find('=');
        if (separator == std::string::npos)
            return Invalid(error, lineNumber, "<line>");
        const std::string key = Trim(std::string_view(trimmed).substr(0, separator));
        const std::string value = Trim(std::string_view(trimmed).substr(separator + 1));
        bool valid = true;
        if (key == "window.title")
            valid = ParseString(value, parsed.Window.title);
        else if (key == "window.width")
            valid = ParseInt(value, parsed.Window.width) && parsed.Window.width > 0;
        else if (key == "window.height")
            valid = ParseInt(value, parsed.Window.height) && parsed.Window.height > 0;
        else if (key == "window.api")
            valid = ParseEnum(value, {{"opengl", GraphicsApi::OpenGL}, {"vulkan", GraphicsApi::Vulkan}},
                              parsed.Window.api);
        else if (key == "window.mode")
            valid = ParseEnum(value,
                              {{"windowed_fixed", WindowMode::WindowedFixed},
                               {"windowed_resizable", WindowMode::WindowedResizable},
                               {"borderless", WindowMode::BorderlessFullscreen},
                               {"fullscreen", WindowMode::ExclusiveFullscreen}},
                              parsed.Window.mode);
        else if (key == "window.monitor")
            valid = ParseInt(value, parsed.Window.monitor);
        else if (key == "window.position_x")
            valid = ParseInt(value, parsed.Window.positionX);
        else if (key == "window.position_y")
            valid = ParseInt(value, parsed.Window.positionY);
        else if (key == "window.center")
            valid = ParseBool(value, parsed.Window.centerOnMonitor);
        else if (key == "window.focus_on_show")
            valid = ParseBool(value, parsed.Window.focusOnShow);
        else if (key == "window.visible")
            valid = ParseBool(value, parsed.Window.visible);
        else if (key == "window.cursor")
            valid = ParseEnum(value,
                              {{"normal", CursorMode::Normal},
                               {"hidden", CursorMode::Hidden},
                               {"captured", CursorMode::Captured}},
                              parsed.Window.cursor);
        else if (key == "renderer.present")
            valid = ParseEnum(value,
                              {{"immediate", PresentMode::Immediate},
                               {"vsync", PresentMode::VSync},
                               {"adaptive", PresentMode::Adaptive}},
                              parsed.Renderer.Presentation);
        else if (key == "renderer.msaa")
            valid = ParseUnsigned(value, parsed.Renderer.MsaaSamples) && parsed.Renderer.MsaaSamples > 0;
        else if (key == "renderer.anisotropy")
        {
            double number = 0.0;
            valid = ParseDouble(value, number) && number >= 1.0;
            if (valid)
                parsed.Renderer.MaxAnisotropy = static_cast<float>(number);
        }
        else if (key == "renderer.prefer_discrete_gpu")
            valid = ParseBool(value, parsed.Renderer.PreferDiscreteGpu);
        else if (key == "renderer.preferred_adapter")
            valid = ParseString(value, parsed.Renderer.PreferredAdapter);
        else if (key == "renderer.validation")
            valid = ParseBool(value, parsed.Renderer.EnableValidation);
        else if (key == "renderer.gpu_timing")
            valid = ParseBool(value, parsed.Renderer.EnableGpuTiming);
        else if (key == "renderer.gpu_policy")
            valid = ParseEnum(value,
                              {{"default", GpuCapabilityPolicy::Default},
                               {"conservative", GpuCapabilityPolicy::Conservative}},
                              parsed.Renderer.CapabilityPolicy);
        else if (key == "renderer.driver_workarounds")
            valid = ParseBool(value, parsed.Renderer.EnableDriverWorkarounds);
        else if (key == "application.unfocused")
            valid = ParseEnum(value,
                              {{"continue", UnfocusedBehavior::Continue},
                               {"render", UnfocusedBehavior::RenderOnly},
                               {"pause", UnfocusedBehavior::Pause}},
                              parsed.Unfocused);
        else if (key == "application.max_delta")
        {
            double number = 0.0;
            valid = ParseDouble(value, number) && number > 0.0;
            if (valid)
                parsed.MaximumDeltaSeconds = static_cast<float>(number);
        }
        else if (key == "application.fixed_delta")
        {
            double number = 0.0;
            valid = ParseDouble(value, number) && number >= 0.0;
            if (valid)
                parsed.FixedDeltaSeconds = static_cast<float>(number);
        }
        else if (key == "application.max_fps")
            valid = ParseDouble(value, parsed.FrameRateLimit) && parsed.FrameRateLimit >= 0.0;
        else if (key == "application.capture_cursor_on_look")
            valid = ParseBool(value, parsed.CaptureCursorOnRightMouse);
        else if (key == "diagnostics.runtime_monitors")
            valid = ParseBool(value, parsed.EnableRuntimeMonitors);
        if (!valid)
            return Invalid(error, lineNumber, key);
    }

    config = std::move(parsed);
    if (error)
        error->clear();
    return true;
}

} // namespace engine
