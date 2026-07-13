#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/debug/DebugOverlay.h"
#include "engine/render/SceneRenderer.h"

#include <algorithm>

#include <glad/gl.h>

namespace engine {

void GLRenderBackend::RefreshGpuFrameTotal()
{
    m_frameStats.GpuShadowMilliseconds = m_directionalShadowMilliseconds
                                       + m_localShadowMilliseconds;
    m_frameStats.GpuFrameMilliseconds = m_frameStats.GpuShadowMilliseconds
                                      + m_frameStats.GpuMainMilliseconds
                                      + m_frameStats.GpuPostMilliseconds;
}

void GLRenderBackend::RenderDebugOverlay(const RenderFrameData& frame)
{
    if (!frame.DebugOverlay || !m_debugOverlayShader || m_debugOverlayTexture == 0)
        return;

    const debug::DebugOverlayImage& overlay = *frame.DebugOverlay;
    glBindTexture(GL_TEXTURE_2D, m_debugOverlayTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    debug::DebugOverlayImage::Width,
                    debug::DebugOverlayImage::Height,
                    GL_RGBA, GL_UNSIGNED_BYTE, overlay.Pixels.data());

    constexpr int margin = 10;
    const int viewportY = std::max(m_height - margin
        - static_cast<int>(debug::DebugOverlayImage::Height), 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(margin, viewportY, debug::DebugOverlayImage::Width,
               debug::DebugOverlayImage::Height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_debugOverlayShader->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_debugOverlayTexture);
    m_debugOverlayShader->SetInt("uOverlay", 0);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glViewport(0, 0, m_width, m_height);
    glEnable(GL_DEPTH_TEST);
}

} // namespace engine
