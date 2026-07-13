#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/debug/DebugOverlay.h"
#include "engine/render/SceneRenderer.h"

#include <algorithm>

#include <glad/gl.h>
#include <glm/vec4.hpp>

namespace engine {

void GLRenderBackend::RenderDebugOverlay(const RenderFrameData& frame)
{
    if (!frame.DebugOverlay || !m_debugOverlayShader || m_debugOverlayTextures[0] == 0)
        return;

    const debug::DebugOverlayImage& overlay = *frame.DebugOverlay;
    const unsigned int overlayTexture = m_debugOverlayTextures[m_debugOverlayTextureIndex];
    glBindTexture(GL_TEXTURE_2D, overlayTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Give the upload fresh storage. Some drivers keep sampling the previous
    // atlas for several frames; replacing its contents in-place can therefore
    // race the outstanding debug draw and intermittently erase fine text.
    // The debugger is an opt-in diagnostic view, so the small orphaning cost is
    // preferable to a fence or a permanently mapped staging ring here.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8,
                 debug::DebugOverlayImage::Width,
                 debug::DebugOverlayImage::Height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, overlay.Pixels.data());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_debugOverlayShader->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, overlayTexture);
    m_debugOverlayShader->SetInt("uOverlay", 0);
    glBindVertexArray(m_emptyVao);
    for (uint32_t index = 0; index < overlay.LayerCount; ++index)
    {
        const debug::DebugOverlayDrawRect rect = debug::ResolveDebugOverlayLayer(
            overlay.Layers[index], static_cast<uint32_t>(std::max(m_width, 0)),
            static_cast<uint32_t>(std::max(m_height, 0)));
        if (rect.Width == 0 || rect.Height == 0)
            continue;
        const int viewportY = std::max(m_height - rect.Y - static_cast<int>(rect.Height), 0);
        glViewport(rect.X, viewportY, static_cast<int>(rect.Width),
                   static_cast<int>(rect.Height));
        m_debugOverlayShader->SetVec4(
            "uUvRect",
            glm::vec4(static_cast<float>(rect.SourceX) / debug::DebugOverlayImage::TextureWidth,
                      static_cast<float>(rect.SourceY) / debug::DebugOverlayImage::TextureHeight,
                      static_cast<float>(rect.Width) / debug::DebugOverlayImage::TextureWidth,
                      static_cast<float>(rect.Height) / debug::DebugOverlayImage::TextureHeight));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        ++m_gpuDrawCallsThisFrame;
    }
    glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glViewport(0, 0, m_width, m_height);
    glEnable(GL_DEPTH_TEST);
    m_debugOverlayTextureIndex = (m_debugOverlayTextureIndex + 1) %
        static_cast<uint32_t>(m_debugOverlayTextures.size());
}

} // namespace engine
