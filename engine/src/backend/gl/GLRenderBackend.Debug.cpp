#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/debug/DebugOverlay.h"
#include "engine/render/SceneRenderer.h"

#include <algorithm>
#include <cstring>

#include <glad/gl.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace engine {

void GLRenderBackend::RenderDebugOverlay(const RenderFrameData& frame)
{
    if (!frame.DebugOverlay || !m_debugOverlayShader || m_debugOverlayTextures[0] == 0)
        return;

    const debug::DebugOverlayImage& overlay = *frame.DebugOverlay;
    if (overlay.Revision != m_debugOverlayRevision)
    {
        uint32_t uploadIndex = m_debugOverlayTextureIndex;
        bool slotAvailable = true;
        if (m_debugOverlayRevision != 0)
        {
            uploadIndex = (m_debugOverlayTextureIndex + 1) %
                static_cast<uint32_t>(m_debugOverlayTextures.size());
            if (m_debugOverlayFences[uploadIndex])
            {
                const GLenum status = glClientWaitSync(
                    reinterpret_cast<GLsync>(m_debugOverlayFences[uploadIndex]), 0, 0);
                slotAvailable = status == GL_ALREADY_SIGNALED ||
                                status == GL_CONDITION_SATISFIED;
                if (slotAvailable)
                {
                    glDeleteSync(reinterpret_cast<GLsync>(
                        m_debugOverlayFences[uploadIndex]));
                    m_debugOverlayFences[uploadIndex] = nullptr;
                }
            }
        }

        if (slotAvailable)
        {
            if (m_debugOverlayRevision != 0)
            {
                // Retire the texture that was sampled by previous frames. The
                // fence is checked only when this ring slot is selected again.
                m_debugOverlayFences[m_debugOverlayTextureIndex] =
                    reinterpret_cast<void*>(glFenceSync(
                        GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
                m_debugOverlayTextureIndex = uploadIndex;
            }
            uint8_t* mapped = m_debugOverlayMappedBuffers[m_debugOverlayTextureIndex];
            for (uint32_t index = 0; index < overlay.LayerCount; ++index)
            {
                const debug::DebugOverlayLayer& layer = overlay.Layers[index];
                for (uint32_t row = 0; row < layer.Height; ++row)
                {
                    const size_t offset =
                        (static_cast<size_t>(layer.SourceY + row) *
                             debug::DebugOverlayImage::TextureWidth +
                         layer.SourceX) * 4;
                    std::memcpy(mapped + offset, overlay.Pixels.data() + offset,
                                static_cast<size_t>(layer.Width) * 4);
                }
            }
            glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
            m_debugOverlayRevision = overlay.Revision;
        }
    }
    const unsigned int overlayTexture = m_debugOverlayTextures[m_debugOverlayTextureIndex];

    glBindFramebuffer(GL_FRAMEBUFFER, m_activeOutputFramebuffer);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_debugOverlayShader->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, overlayTexture);
    m_debugOverlayShader->SetInt("uOverlay", 0);
    m_debugOverlayShader->SetVec2(
        "uAtlasSize",
        glm::vec2(static_cast<float>(debug::DebugOverlayImage::TextureWidth),
                  static_cast<float>(debug::DebugOverlayImage::TextureHeight)));
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
}

} // namespace engine
