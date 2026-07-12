#include "engine/backend/gl/GLRenderBackend.h"

#include <glad/gl.h>

#include <algorithm>
#include <stdexcept>

namespace engine {

namespace {

constexpr int kBloomLevels = 6;

} // namespace

void GLRenderBackend::InitShadowMap()
{
    glGenFramebuffers(1, &m_shadowFbo);
    glGenTextures(kShadowCascadeCount, m_shadowMaps.data());
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade)
    {
        glBindTexture(GL_TEXTURE_2D, m_shadowMaps[cascade]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, m_shadowSizes[cascade], m_shadowSizes[cascade],
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowMaps[cascade], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cascade shadow framebuffer incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLRenderBackend::CreateSceneTargets(int width, int height)
{
    DestroySceneTargets();

    glGenRenderbuffers(1, &m_msaaColorRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_msaaSamples, GL_RGBA16F, width, height);

    glGenRenderbuffers(1, &m_msaaDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_msaaSamples, GL_DEPTH_COMPONENT32F, width, height);

    glGenRenderbuffers(1, &m_msaaVelocityRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaVelocityRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_msaaSamples, GL_RG16F, width, height);

    glGenFramebuffers(1, &m_msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColorRbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, m_msaaVelocityRbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepthRbo);
    constexpr GLenum sceneDrawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, sceneDrawBuffers);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("MSAA HDR framebuffer incomplete");

    glGenTextures(1, &m_hdrColorTex);
    glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &m_velocityTex);
    glBindTexture(GL_TEXTURE_2D, m_velocityTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &m_depthTex);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hdrColorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_velocityTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);
    glDrawBuffers(2, sceneDrawBuffers);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Resolve framebuffer incomplete");

    for (int i = 0; i < 2; ++i)
    {
        glGenTextures(1, &m_taaHistoryColor[i]);
        glBindTexture(GL_TEXTURE_2D, m_taaHistoryColor[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenTextures(1, &m_taaHistoryDepth[i]);
        glBindTexture(GL_TEXTURE_2D, m_taaHistoryDepth[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &m_taaFbos[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_taaFbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_taaHistoryColor[i], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_taaHistoryDepth[i], 0);
        glDrawBuffers(2, sceneDrawBuffers);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("TAA history framebuffer incomplete");
    }

    glGenTextures(1, &m_postColorTex);
    glBindTexture(GL_TEXTURE_2D, m_postColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &m_postFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_postFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_postColorTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Post-process framebuffer incomplete");

    int w = std::max(width / 2, 1);
    int h = std::max(height / 2, 1);
    for (int i = 0; i < kBloomLevels && w >= 8 && h >= 8; ++i)
    {
        BloomLevel level;
        level.Width = w;
        level.Height = h;
        glGenTextures(1, &level.Texture);
        glBindTexture(GL_TEXTURE_2D, level.Texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &level.Fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, level.Fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, level.Texture, 0);
        m_bloomChain.push_back(level);
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_taaHistoryIndex = 0;
    m_taaHistoryValid = false;
    m_taaFrameIndex = 0;
    m_previousTransforms.clear();
}

void GLRenderBackend::DestroySceneTargets()
{
    for (auto& level : m_bloomChain)
    {
        glDeleteFramebuffers(1, &level.Fbo);
        glDeleteTextures(1, &level.Texture);
    }
    m_bloomChain.clear();

    if (m_postFbo) { glDeleteFramebuffers(1, &m_postFbo); m_postFbo = 0; }
    if (m_postColorTex) { glDeleteTextures(1, &m_postColorTex); m_postColorTex = 0; }
    glDeleteFramebuffers(2, m_taaFbos.data());
    glDeleteTextures(2, m_taaHistoryColor.data());
    glDeleteTextures(2, m_taaHistoryDepth.data());
    m_taaFbos.fill(0);
    m_taaHistoryColor.fill(0);
    m_taaHistoryDepth.fill(0);

    if (m_resolveFbo) { glDeleteFramebuffers(1, &m_resolveFbo); m_resolveFbo = 0; }
    if (m_hdrColorTex) { glDeleteTextures(1, &m_hdrColorTex); m_hdrColorTex = 0; }
    if (m_velocityTex) { glDeleteTextures(1, &m_velocityTex); m_velocityTex = 0; }
    if (m_depthTex) { glDeleteTextures(1, &m_depthTex); m_depthTex = 0; }
    if (m_msaaFbo) { glDeleteFramebuffers(1, &m_msaaFbo); m_msaaFbo = 0; }
    if (m_msaaColorRbo) { glDeleteRenderbuffers(1, &m_msaaColorRbo); m_msaaColorRbo = 0; }
    if (m_msaaVelocityRbo) { glDeleteRenderbuffers(1, &m_msaaVelocityRbo); m_msaaVelocityRbo = 0; }
    if (m_msaaDepthRbo) { glDeleteRenderbuffers(1, &m_msaaDepthRbo); m_msaaDepthRbo = 0; }
}

} // namespace engine
