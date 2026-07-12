#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/core/Log.h"

#include <glad/gl.h>
#include <stb_image_write.h>

#include <cstdint>
#include <vector>

namespace engine {

void GLRenderBackend::RenderBloom(float threshold, float exposure)
{
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_emptyVao);

    m_bloomDownShader->Use();
    m_bloomDownShader->SetInt("uSource", 0);
    m_bloomDownShader->SetFloat("uThreshold", threshold);
    m_bloomDownShader->SetFloat("uExposure", exposure);
    glActiveTexture(GL_TEXTURE0);

    for (size_t i = 0; i < m_bloomChain.size(); ++i)
    {
        const BloomLevel& level = m_bloomChain[i];
        glBindFramebuffer(GL_FRAMEBUFFER, level.Fbo);
        glViewport(0, 0, level.Width, level.Height);
        glBindTexture(GL_TEXTURE_2D, i == 0 ? m_hdrColorTex : m_bloomChain[i - 1].Texture);
        m_bloomDownShader->SetBool("uFirstPass", i == 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    m_bloomUpShader->Use();
    m_bloomUpShader->SetInt("uSource", 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (size_t i = m_bloomChain.size() - 1; i >= 1; --i)
    {
        const BloomLevel& target = m_bloomChain[i - 1];
        glBindFramebuffer(GL_FRAMEBUFFER, target.Fbo);
        glViewport(0, 0, target.Width, target.Height);
        glBindTexture(GL_TEXTURE_2D, m_bloomChain[i].Texture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void GLRenderBackend::SaveScreenshot()
{
    std::vector<uint8_t> pixels(static_cast<size_t>(m_width) * m_height * 3);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_width, m_height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    stbi_flip_vertically_on_write(1);
    if (stbi_write_png(m_screenshotPath.c_str(), m_width, m_height, 3, pixels.data(), m_width * 3))
        log::Info("Saved screenshot: " + m_screenshotPath);
    else
        log::Error("Failed to save screenshot: " + m_screenshotPath);
    m_screenshotPath.clear();
}

} // namespace engine
