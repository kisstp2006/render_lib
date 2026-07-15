#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/backend/gl/GLDebug.h"
#include "engine/core/Window.h"
#include "engine/render/SceneRenderer.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace engine
{

struct GLRenderBackend::ViewTargetState
{
    int Width = 1;
    int Height = 1;
    float AutoExposure = 1.0f;
    double LastFrameTime = 0.0;
    int ExposurePboIndex = 0;
    int ExposurePboFrames = 0;

    unsigned int MsaaFbo = 0;
    unsigned int MsaaColorRbo = 0;
    unsigned int MsaaVelocityRbo = 0;
    unsigned int MsaaDepthRbo = 0;
    unsigned int ResolveFbo = 0;
    unsigned int HdrColor = 0;
    unsigned int Velocity = 0;
    unsigned int Depth = 0;
    unsigned int HiZ = 0;
    int HiZMips = 1;
    bool HiZValid = false;
    glm::mat4 HiZViewProjection{1.0f};

    std::array<OcclusionReadbackSlot, kOcclusionReadbackSlots> OcclusionSlots{};
    uint32_t OcclusionWriteSlot = 0;
    TemporalOcclusionState OcclusionState;
    std::unordered_set<uint32_t> OcclusionCulled;

    std::array<unsigned int, 2> TaaFbos{};
    std::array<unsigned int, 2> TaaColor{};
    std::array<unsigned int, 2> TaaDepth{};
    int TaaHistoryIndex = 0;
    bool TaaHistoryValid = false;
    uint64_t TaaFrameIndex = 0;
    AntiAliasingMode PreviousAa = AntiAliasingMode::None;
    glm::mat4 PreviousViewProjection{1.0f};
    glm::vec3 PreviousCameraPosition{0.0f};
    glm::vec3 PreviousCameraForward{0.0f, 0.0f, -1.0f};
    float PreviousCameraFov = 60.0f;
    const Scene* PreviousScene = nullptr;
    std::unordered_map<uint64_t, glm::mat4> PreviousTransforms;

    unsigned int PostFbo = 0;
    unsigned int PostColor = 0;
    std::vector<BloomLevel> Bloom;
    bool HasFrameDebugFrame = false;
};

struct GLRenderBackend::OffscreenViewport
{
    RenderViewportDesc Desc;
    ViewTargetState Targets;
    unsigned int OutputTexture = 0;
    unsigned int OutputFramebuffer = 0;
    uint64_t Generation = 1;
};

struct GLRenderBackend::ViewportStorage
{
    std::unordered_map<uint64_t, std::unique_ptr<OffscreenViewport>> Items;
};

GLRenderBackend::GLRenderBackend() = default;
GLRenderBackend::~GLRenderBackend() = default;

void GLRenderBackend::SwapViewTargetState(ViewTargetState& state)
{
    using std::swap;
    swap(m_width, state.Width);
    swap(m_height, state.Height);
    swap(m_autoExposure, state.AutoExposure);
    swap(m_lastFrameTime, state.LastFrameTime);
    swap(m_exposurePboIndex, state.ExposurePboIndex);
    swap(m_exposurePboFrames, state.ExposurePboFrames);
    swap(m_msaaFbo, state.MsaaFbo);
    swap(m_msaaColorRbo, state.MsaaColorRbo);
    swap(m_msaaVelocityRbo, state.MsaaVelocityRbo);
    swap(m_msaaDepthRbo, state.MsaaDepthRbo);
    swap(m_resolveFbo, state.ResolveFbo);
    swap(m_hdrColorTex, state.HdrColor);
    swap(m_velocityTex, state.Velocity);
    swap(m_depthTex, state.Depth);
    swap(m_hizTexture, state.HiZ);
    swap(m_hizMipLevels, state.HiZMips);
    swap(m_hizValid, state.HiZValid);
    swap(m_hizViewProjection, state.HiZViewProjection);
    swap(m_occlusionSlots, state.OcclusionSlots);
    swap(m_occlusionWriteSlot, state.OcclusionWriteSlot);
    swap(m_occlusionState, state.OcclusionState);
    swap(m_occlusionCulledInstances, state.OcclusionCulled);
    swap(m_taaFbos, state.TaaFbos);
    swap(m_taaHistoryColor, state.TaaColor);
    swap(m_taaHistoryDepth, state.TaaDepth);
    swap(m_taaHistoryIndex, state.TaaHistoryIndex);
    swap(m_taaHistoryValid, state.TaaHistoryValid);
    swap(m_taaFrameIndex, state.TaaFrameIndex);
    swap(m_previousAaMode, state.PreviousAa);
    swap(m_previousViewProjection, state.PreviousViewProjection);
    swap(m_previousCameraPosition, state.PreviousCameraPosition);
    swap(m_previousCameraForward, state.PreviousCameraForward);
    swap(m_previousCameraFov, state.PreviousCameraFov);
    swap(m_previousScene, state.PreviousScene);
    swap(m_previousTransforms, state.PreviousTransforms);
    swap(m_postFbo, state.PostFbo);
    swap(m_postColorTex, state.PostColor);
    swap(m_bloomChain, state.Bloom);
    swap(m_hasFrameDebugFrame, state.HasFrameDebugFrame);
}

void GLRenderBackend::CreateViewportOutput(OffscreenViewport& viewport)
{
    const int width = static_cast<int>(std::max(viewport.Desc.Width, 1u));
    const int height = static_cast<int>(std::max(viewport.Desc.Height, 1u));
    glGenTextures(1, &viewport.OutputTexture);
    glBindTexture(GL_TEXTURE_2D, viewport.OutputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_debug::LabelObject(GL_TEXTURE, viewport.OutputTexture,
                          viewport.Desc.Name + " Output");

    glGenFramebuffers(1, &viewport.OutputFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, viewport.OutputFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           viewport.OutputTexture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        DestroyViewportOutput(viewport);
        throw std::runtime_error("OpenGL: offscreen viewport framebuffer is incomplete");
    }
    gl_debug::LabelObject(GL_FRAMEBUFFER, viewport.OutputFramebuffer,
                          viewport.Desc.Name + " Output FBO");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLRenderBackend::DestroyViewportOutput(OffscreenViewport& viewport)
{
    if (viewport.OutputFramebuffer)
        glDeleteFramebuffers(1, &viewport.OutputFramebuffer);
    if (viewport.OutputTexture)
        glDeleteTextures(1, &viewport.OutputTexture);
    viewport.OutputFramebuffer = 0;
    viewport.OutputTexture = 0;
}

void GLRenderBackend::CreateViewportResources(OffscreenViewport& viewport)
{
    CreateViewportOutput(viewport);
    bool stateIsActive = false;
    try
    {
        // Build a completely independent set of temporal/depth/post targets,
        // then restore the main-window view without reallocating it.
        SwapViewTargetState(viewport.Targets);
        stateIsActive = true;
        m_width = static_cast<int>(viewport.Desc.Width);
        m_height = static_cast<int>(viewport.Desc.Height);
        CreateSceneTargets(m_width, m_height);
        SwapViewTargetState(viewport.Targets);
        stateIsActive = false;
    }
    catch (...)
    {
        if (stateIsActive)
        {
            DestroyOcclusionResources();
            DestroySceneTargets();
            SwapViewTargetState(viewport.Targets);
        }
        DestroyViewportOutput(viewport);
        throw;
    }
}

void GLRenderBackend::DestroyViewportResources(OffscreenViewport& viewport)
{
    DestroyViewportOutput(viewport);
    SwapViewTargetState(viewport.Targets);
    DestroyOcclusionResources();
    DestroySceneTargets();
    SwapViewTargetState(viewport.Targets);
}

RenderViewportHandle GLRenderBackend::CreateViewport(const RenderViewportDesc& requested)
{
    if (!m_window)
        return {};
    if (!m_viewports)
        m_viewports = std::make_unique<ViewportStorage>();
    auto viewport = std::make_unique<OffscreenViewport>();
    viewport->Desc = requested;
    viewport->Desc.Width = std::max(requested.Width, 1u);
    viewport->Desc.Height = std::max(requested.Height, 1u);
    CreateViewportResources(*viewport);

    const RenderViewportHandle handle{m_nextViewportId++};
    m_viewports->Items.emplace(handle.Value, std::move(viewport));
    return handle;
}

bool GLRenderBackend::ResizeViewport(RenderViewportHandle handle, uint32_t width,
                                     uint32_t height)
{
    if (!m_viewports)
        return false;
    const auto found = m_viewports->Items.find(handle.Value);
    if (found == m_viewports->Items.end() || width == 0 || height == 0)
        return false;
    OffscreenViewport& viewport = *found->second;
    if (viewport.Desc.Width == width && viewport.Desc.Height == height)
        return true;
    // Allocate the replacement first. An allocation failure leaves the old
    // viewport fully valid and displayable instead of half-resized.
    OffscreenViewport replacement;
    replacement.Desc = viewport.Desc;
    replacement.Desc.Width = width;
    replacement.Desc.Height = height;
    replacement.Generation = viewport.Generation + 1;
    CreateViewportResources(replacement);
    glFinish();
    DestroyViewportResources(viewport);
    viewport = std::move(replacement);
    return true;
}

void GLRenderBackend::DestroyViewport(RenderViewportHandle handle)
{
    if (!m_viewports)
        return;
    const auto found = m_viewports->Items.find(handle.Value);
    if (found == m_viewports->Items.end())
        return;
    glFinish();
    OffscreenViewport& viewport = *found->second;
    DestroyViewportResources(viewport);
    m_viewports->Items.erase(found);
}

bool GLRenderBackend::RenderViewport(RenderViewportHandle handle,
                                     const RenderFrameData& frame)
{
    if (!m_viewports)
        return false;
    const auto found = m_viewports->Items.find(handle.Value);
    if (found == m_viewports->Items.end() || !frame.SceneData || !frame.CameraData)
        return false;
    OffscreenViewport& viewport = *found->second;
    if (frame.Width != static_cast<int>(viewport.Desc.Width) ||
        frame.Height != static_cast<int>(viewport.Desc.Height))
        return false;

    SwapViewTargetState(viewport.Targets);
    m_activeOutputFramebuffer = viewport.OutputFramebuffer;
    m_renderingOffscreen = true;
    try
    {
        RenderFrame(frame);
    }
    catch (...)
    {
        m_renderingOffscreen = false;
        m_activeOutputFramebuffer = 0;
        SwapViewTargetState(viewport.Targets);
        throw;
    }
    m_renderingOffscreen = false;
    m_activeOutputFramebuffer = 0;
    SwapViewTargetState(viewport.Targets);
    return true;
}

RenderTextureHandle GLRenderBackend::GetViewportTexture(RenderViewportHandle handle) const
{
    if (!m_viewports)
        return {};
    const auto found = m_viewports->Items.find(handle.Value);
    if (found == m_viewports->Items.end())
        return {};
    const OffscreenViewport& viewport = *found->second;
    return {RenderBackendApi::OpenGL, handle, viewport.OutputTexture, 0, 0,
            viewport.Desc.Width, viewport.Desc.Height, viewport.Generation};
}

NativeGraphicsContext GLRenderBackend::GetNativeGraphicsContext() const
{
    NativeGraphicsContext result;
    result.Api = RenderBackendApi::OpenGL;
    result.Window = m_window ? m_window->Handle() : nullptr;
    return result;
}

void GLRenderBackend::WaitIdle()
{
    if (m_window)
        glFinish();
}

void GLRenderBackend::DestroyAllViewports()
{
    if (!m_viewports)
        return;
    while (!m_viewports->Items.empty())
        DestroyViewport({m_viewports->Items.begin()->first});
    m_viewports.reset();
}

} // namespace engine
