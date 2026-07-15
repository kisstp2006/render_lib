#pragma once

#include "engine/backend/IRenderBackend.h"

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace engine
{
class Window;
}

namespace engine::editor
{

struct ImGuiLayerConfig
{
    bool Docking = true;
    bool KeyboardNavigation = true;
    // Native OS platform windows are optional. Multiple engine scene
    // viewports work independently of this flag through RenderViewportHandle.
    bool PlatformViewports = false;
    // Empty disables ImGui ini persistence. The default lives under the
    // engine cache so tests and samples do not dirty the source tree.
    std::string IniFilename = ".cache/editor/imgui.ini";
};

class ImGuiLayer
{
public:
    using BuildCallback = std::function<void()>;

    ImGuiLayer() = default;
    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    bool Initialize(Window& window, IRenderBackend& backend,
                    const ImGuiLayerConfig& config = {});
    void Shutdown();
    void BeginFrame();
    void EndFrame();

    void SetBuildCallback(BuildCallback callback)
    {
        m_buildCallback = std::move(callback);
    }
    bool WantsKeyboard() const;
    bool WantsMouse() const;
    bool IsInitialized() const noexcept { return m_initialized; }

    ImTextureID TextureId(const RenderTextureHandle& texture);
    void ForgetTexture(RenderViewportHandle viewport);
    void DrawViewportImage(const RenderTextureHandle& texture,
                           const ImVec2& size,
                           const ImVec4& tint = ImVec4(1, 1, 1, 1));

private:
    struct TextureBinding
    {
        uint64_t Generation = 0;
        ImTextureID Id = 0;
    };

    void Render(const NativeUiRenderContext& context);

    IRenderBackend* m_backend = nullptr;
    RenderBackendApi m_api = RenderBackendApi::OpenGL;
    BuildCallback m_buildCallback;
    std::unordered_map<uint64_t, TextureBinding> m_textureBindings;
    std::string m_iniFilename;
    bool m_initialized = false;
    bool m_frameBegun = false;
    bool m_drawDataReady = false;
    bool m_platformViewports = false;
};

} // namespace engine::editor
