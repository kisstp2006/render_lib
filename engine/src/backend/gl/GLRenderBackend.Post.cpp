#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/scene/Scene.h"

#include <glad/gl.h>
#include <stb_image_write.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace engine {

void GLRenderBackend::RenderBloom(unsigned int sourceTexture, float threshold, float exposure)
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
        glBindTexture(GL_TEXTURE_2D, i == 0 ? sourceTexture : m_bloomChain[i - 1].Texture);
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

unsigned int GLRenderBackend::ResolveTemporalAA(const Scene& scene, const Camera& camera,
                                                const glm::mat4&, const glm::mat4&,
                                                const glm::mat4& jitteredViewProjection)
{
    const PostProcessSettings& pp = scene.PostProcess;
    const int writeIndex = (m_taaHistoryIndex + 1) % 2;
    glBindFramebuffer(GL_FRAMEBUFFER, m_taaFbos[writeIndex]);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);
    m_taaShader->Use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
    m_taaShader->SetInt("uCurrentColor", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_velocityTex);
    m_taaShader->SetInt("uVelocity", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    m_taaShader->SetInt("uCurrentDepth", 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_taaHistoryColor[m_taaHistoryIndex]);
    m_taaShader->SetInt("uHistoryColor", 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_taaHistoryDepth[m_taaHistoryIndex]);
    m_taaShader->SetInt("uHistoryDepth", 4);
    m_taaShader->SetBool("uHistoryValid", m_taaHistoryValid);
    m_taaShader->SetFloat("uHistoryWeight", pp.TaaHistoryWeight);
    m_taaShader->SetFloat("uDepthThreshold", pp.TaaDepthThreshold);
    m_taaShader->SetFloat("uSharpen", pp.TaaSharpen);
    m_taaShader->SetFloat("uNearPlane", camera.NearPlane);
    m_taaShader->SetFloat("uFarPlane", camera.FarPlane);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    m_taaHistoryIndex = writeIndex;
    m_taaHistoryValid = true;
    m_previousViewProjection = jitteredViewProjection;
    m_previousCameraPosition = camera.Position;
    m_previousCameraForward = camera.Forward();
    m_previousCameraFov = camera.FovDegrees;
    m_previousScene = &scene;
    m_previousTransforms.clear();
    for (const MeshInstance& instance : scene.Instances())
        if (instance.TemporalId != 0)
            m_previousTransforms[instance.TemporalId] = instance.Transform;
    return m_taaHistoryColor[writeIndex];
}

unsigned int GLRenderBackend::GetOrCreateColorLut(const std::shared_ptr<ColorGradingLutData>& data)
{
    if (const auto found = m_colorLutCache.find(data); found != m_colorLutCache.end())
        return found->second;
    if (!data || data->Size < 2
        || data->Values.size() != static_cast<size_t>(data->Size) * data->Size * data->Size)
        throw std::runtime_error("Cannot upload invalid 3D color grading LUT");

    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_3D, texture);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, data->Size, data->Size, data->Size,
                 0, GL_RGB, GL_FLOAT, data->Values.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    m_colorLutCache.emplace(data, texture);
    log::Info("Uploaded " + std::to_string(data->Size) + "^3 color LUT: " + data->SourcePath);
    return texture;
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
