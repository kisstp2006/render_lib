#include "engine/backend/gl/GLRenderBackend.h"
#include "engine/backend/gl/GLDebug.h"

#include "engine/scene/Scene.h"
#include "engine/core/Log.h"
#include "engine/render/SceneRenderer.h"
#include "engine/profiling/CpuProfiler.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace engine {

namespace {

constexpr int kUnitAlbedo = 1;
constexpr int kUnitLocalShadowAtlas = 13;
constexpr int kUnitPointShadowArray = 14;
constexpr int kUnitCookieAtlas = 15;

glm::vec4 AtlasRect(int slot, int tileSize, int atlasSize)
{
    const int columns = atlasSize / tileSize;
    const float scale = static_cast<float>(tileSize) / atlasSize;
    return {static_cast<float>(slot % columns) * scale, static_cast<float>(slot / columns) * scale, scale, scale};
}

} // namespace

void GLRenderBackend::InitLocalLightResources()
{
    glGenFramebuffers(1, &m_localShadowAtlasFbo);
    glGenTextures(1, &m_localShadowAtlas);
    glBindTexture(GL_TEXTURE_2D, m_localShadowAtlas);
    gl_debug::LabelObject(GL_TEXTURE, m_localShadowAtlas, "Spot + Area Shadow Atlas");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, kLocalShadowAtlasSize, kLocalShadowAtlasSize,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float depthBorder[] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, depthBorder);
    glBindFramebuffer(GL_FRAMEBUFFER, m_localShadowAtlasFbo);
    gl_debug::LabelObject(GL_FRAMEBUFFER, m_localShadowAtlasFbo, "Local Shadow Atlas FBO");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_localShadowAtlas, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Local shadow atlas framebuffer incomplete");

    glGenFramebuffers(1, &m_pointShadowFbo);
    glGenTextures(1, &m_pointShadowArray);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_pointShadowArray);
    gl_debug::LabelObject(GL_TEXTURE, m_pointShadowArray, "Point Shadow Cube Array");
    glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT32F, m_pointShadowSize, m_pointShadowSize,
                 kMaxPointShadows * 6, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFbo);
    gl_debug::LabelObject(GL_FRAMEBUFFER, m_pointShadowFbo, "Point Shadow Cube Array FBO");
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_pointShadowArray, 0, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Point shadow array framebuffer incomplete");

    glGenTextures(1, &m_cookieAtlas);
    glBindTexture(GL_TEXTURE_2D, m_cookieAtlas);
    gl_debug::LabelObject(GL_TEXTURE, m_cookieAtlas, "Local Light Cookie Atlas");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kCookieAtlasSize, kCookieAtlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLRenderBackend::DestroyLocalLightResources()
{
    if (m_cookieAtlas) glDeleteTextures(1, &m_cookieAtlas);
    if (m_pointShadowArray) glDeleteTextures(1, &m_pointShadowArray);
    if (m_pointShadowFbo) glDeleteFramebuffers(1, &m_pointShadowFbo);
    if (m_localShadowAtlas) glDeleteTextures(1, &m_localShadowAtlas);
    if (m_localShadowAtlasFbo) glDeleteFramebuffers(1, &m_localShadowAtlasFbo);
    m_cookieAtlas = 0;
    m_pointShadowArray = 0;
    m_pointShadowFbo = 0;
    m_localShadowAtlas = 0;
    m_localShadowAtlasFbo = 0;
    m_cookieSlots.clear();
}

void GLRenderBackend::UpdateLightCookieAtlas()
{
    std::unordered_map<const TextureData*, int> desired;
    int nextSlot = 1;
    auto add = [&](const std::shared_ptr<TextureData>& cookie) {
        if (cookie && !desired.contains(cookie.get()) && nextSlot < (kCookieAtlasSize / kCookieTileSize) * (kCookieAtlasSize / kCookieTileSize))
            desired.emplace(cookie.get(), nextSlot++);
    };
    for (int i = 0; i < m_localLights.PointCount; ++i) add(m_localLights.Points[i]->Cookie);
    for (int i = 0; i < m_localLights.SpotCount; ++i) add(m_localLights.Spots[i]->Cookie);
    for (int i = 0; i < m_localLights.AreaCount; ++i) add(m_localLights.Areas[i]->Cookie);
    if (desired == m_cookieSlots)
        return;

    m_cookieSlots = std::move(desired);
    const uint8_t white = 255;
    glClearTexImage(m_cookieAtlas, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
    std::vector<uint8_t> tile(static_cast<size_t>(kCookieTileSize) * kCookieTileSize);
    for (const auto& [texture, slot] : m_cookieSlots)
    {
        for (int y = 0; y < kCookieTileSize; ++y)
        {
            const int sourceY = std::min(y * texture->Height / kCookieTileSize, texture->Height - 1);
            for (int x = 0; x < kCookieTileSize; ++x)
            {
                const int sourceX = std::min(x * texture->Width / kCookieTileSize, texture->Width - 1);
                tile[static_cast<size_t>(y) * kCookieTileSize + x] =
                    texture->Pixels[(static_cast<size_t>(sourceY) * texture->Width + sourceX) * texture->Channels];
            }
        }
        const glm::vec4 rect = AtlasRect(slot, kCookieTileSize, kCookieAtlasSize);
        glTextureSubImage2D(m_cookieAtlas, 0, static_cast<int>(rect.x * kCookieAtlasSize), static_cast<int>(rect.y * kCookieAtlasSize),
                            kCookieTileSize, kCookieTileSize, GL_RED, GL_UNSIGNED_BYTE, tile.data());
    }
}

void GLRenderBackend::RenderLocalLightShadows(
    const RenderFrameData& frame,
    const InstanceBatchBuildResult& shadowBatches)
{
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("LocalLightShadows", "Renderer/OpenGL");
    m_localLights = {};
    m_localLights.PointShadowSlots.fill(-1);
    m_localLights.PointCookieSlots.fill(0);
    m_localLights.SpotCookieSlots.fill(0);
    m_localLights.AreaCookieSlots.fill(0);

    m_localLights.PointCount = static_cast<int>(frame.LocalLights.PointCount);
    for (int i = 0; i < m_localLights.PointCount; ++i)
        m_localLights.Points[i] = frame.LocalLights.Points[i].Source;
    m_localLights.SpotCount = static_cast<int>(frame.LocalLights.SpotCount);
    for (int i = 0; i < m_localLights.SpotCount; ++i)
    {
        m_localLights.Spots[i] = frame.LocalLights.Spots[i].Source;
        m_localLights.SpotDirections[i] = frame.LocalLights.Spots[i].Direction;
        m_localLights.SpotMatrices[i] = frame.LocalLights.Spots[i].Projection;
    }
    m_localLights.AreaCount = static_cast<int>(frame.LocalLights.AreaCount);
    for (int i = 0; i < m_localLights.AreaCount; ++i)
    {
        m_localLights.Areas[i] = frame.LocalLights.Areas[i].Source;
        m_localLights.AreaDirections[i] = frame.LocalLights.Areas[i].Direction;
        m_localLights.AreaRights[i] = frame.LocalLights.Areas[i].Right;
        m_localLights.AreaUps[i] = frame.LocalLights.Areas[i].Up;
        m_localLights.AreaMatrices[i] = frame.LocalLights.Areas[i].Projection;
    }
    UpdateLightCookieAtlas();

    for (int i = 0; i < m_localLights.PointCount; ++i)
        if (const auto found = m_cookieSlots.find(m_localLights.Points[i]->Cookie.get()); found != m_cookieSlots.end()) m_localLights.PointCookieSlots[i] = found->second;
    for (int i = 0; i < m_localLights.SpotCount; ++i)
        if (const auto found = m_cookieSlots.find(m_localLights.Spots[i]->Cookie.get()); found != m_cookieSlots.end()) m_localLights.SpotCookieSlots[i] = found->second;
    for (int i = 0; i < m_localLights.AreaCount; ++i)
        if (const auto found = m_cookieSlots.find(m_localLights.Areas[i]->Cookie.get()); found != m_cookieSlots.end()) m_localLights.AreaCookieSlots[i] = found->second;

    int pointShadowSlot = 0;
    const std::array<glm::vec3, 6> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    const std::array<glm::vec3, 6> ups{{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}}};
    glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFbo);
    glViewport(0, 0, m_pointShadowSize, m_pointShadowSize);
    m_pointShadowShader->Use();
    glCullFace(GL_FRONT);
    for (int lightIndex = 0; lightIndex < m_localLights.PointCount && pointShadowSlot < kMaxPointShadows; ++lightIndex)
    {
        const PointLight& light = *m_localLights.Points[lightIndex];
        if (!light.CastsShadows)
            continue;
        m_localLights.PointShadowSlots[lightIndex] = pointShadowSlot;
        m_pointShadowShader->SetVec3("uLightPosition", light.Position);
        m_pointShadowShader->SetFloat("uLightRange", light.Radius);
        const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, light.Radius);
        for (int face = 0; face < 6; ++face)
        {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_pointShadowArray, 0, pointShadowSlot * 6 + face);
            glClear(GL_DEPTH_BUFFER_BIT);
            m_pointShadowShader->SetMat4("uFaceMatrix", projection * glm::lookAt(light.Position, light.Position + directions[face], ups[face]));
            for (const InstanceDrawBatch& batch : shadowBatches.Batches)
            {
                const MeshInstance& instance = *batch.Representative->Source;
                if (!instance.CastsShadows)
                    continue;
                const Material& material = instance.Mat;
                m_pointShadowShader->SetBool("uAlphaMasked", material.Alpha == Material::AlphaMode::Mask);
                m_pointShadowShader->SetBool("uHasAlbedoMap", material.AlbedoMap != nullptr);
                m_pointShadowShader->SetFloat("uBaseColorAlpha", material.BaseColorAlpha);
                m_pointShadowShader->SetFloat("uAlphaCutoff", material.AlphaCutoff);
                m_pointShadowShader->SetInt("uAlbedoMap", kUnitAlbedo);
                BindMaterialTexture(material.AlbedoMap, kUnitAlbedo, *m_defaultWhite);
                GetOrCreateMesh(instance.Mesh).DrawInstanced(
                    static_cast<uint32_t>(batch.Commands.size()), batch.FirstInstance);
                ++m_gpuDrawCallsThisFrame;
            }
        }
        ++pointShadowSlot;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_localShadowAtlasFbo);
    glViewport(0, 0, kLocalShadowAtlasSize, kLocalShadowAtlasSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    m_shadowShader->Use();
    int atlasSlot = 0;
    auto renderProjectedShadow = [&](const glm::mat4& matrix, glm::vec4& rect) {
        rect = AtlasRect(atlasSlot++, kLocalShadowTileSize, kLocalShadowAtlasSize);
        glViewport(static_cast<int>(rect.x * kLocalShadowAtlasSize), static_cast<int>(rect.y * kLocalShadowAtlasSize),
                   kLocalShadowTileSize, kLocalShadowTileSize);
        m_shadowShader->SetMat4("uLightSpaceMatrix", matrix);
        for (const InstanceDrawBatch& batch : shadowBatches.Batches)
        {
            const MeshInstance& instance = *batch.Representative->Source;
            if (!instance.CastsShadows)
                continue;
            const Material& material = instance.Mat;
            m_shadowShader->SetBool("uAlphaMasked", material.Alpha == Material::AlphaMode::Mask);
            m_shadowShader->SetBool("uHasAlbedoMap", material.AlbedoMap != nullptr);
            m_shadowShader->SetFloat("uBaseColorAlpha", material.BaseColorAlpha);
            m_shadowShader->SetFloat("uAlphaCutoff", material.AlphaCutoff);
            m_shadowShader->SetInt("uAlbedoMap", kUnitAlbedo);
            BindMaterialTexture(material.AlbedoMap, kUnitAlbedo, *m_defaultWhite);
            GetOrCreateMesh(instance.Mesh).DrawInstanced(
                static_cast<uint32_t>(batch.Commands.size()), batch.FirstInstance);
            ++m_gpuDrawCallsThisFrame;
        }
    };

    for (int i = 0; i < m_localLights.SpotCount; ++i)
    {
        const SpotLight& light = *m_localLights.Spots[i];
        if (light.CastsShadows)
            renderProjectedShadow(m_localLights.SpotMatrices[i], m_localLights.SpotShadowRects[i]);
    }
    for (int i = 0; i < m_localLights.AreaCount; ++i)
    {
        const AreaLight& light = *m_localLights.Areas[i];
        if (light.CastsShadows)
            renderProjectedShadow(m_localLights.AreaMatrices[i], m_localLights.AreaShadowRects[i]);
    }
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLRenderBackend::BindLocalLights()
{
    glActiveTexture(GL_TEXTURE0 + kUnitLocalShadowAtlas);
    glBindTexture(GL_TEXTURE_2D, m_localShadowAtlas);
    m_pbrShader->SetInt("uLocalShadowAtlas", kUnitLocalShadowAtlas);
    glActiveTexture(GL_TEXTURE0 + kUnitPointShadowArray);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_pointShadowArray);
    m_pbrShader->SetInt("uPointShadowMaps", kUnitPointShadowArray);
    glActiveTexture(GL_TEXTURE0 + kUnitCookieAtlas);
    glBindTexture(GL_TEXTURE_2D, m_cookieAtlas);
    m_pbrShader->SetInt("uLightCookieAtlas", kUnitCookieAtlas);

    m_pbrShader->SetInt("uPointLightCount", m_localLights.PointCount);
    for (int i = 0; i < m_localLights.PointCount; ++i)
    {
        const PointLight& light = *m_localLights.Points[i];
        const std::string base = "uPointLights[" + std::to_string(i) + "].";
        m_pbrShader->SetVec3(base + "Position", light.Position);
        m_pbrShader->SetVec3(base + "Color", light.Color * light.Intensity);
        m_pbrShader->SetFloat(base + "Radius", light.Radius);
        m_pbrShader->SetInt(base + "ShadowIndex", m_localLights.PointShadowSlots[i]);
        m_pbrShader->SetInt(base + "CookieIndex", m_localLights.PointCookieSlots[i]);
    }

    m_pbrShader->SetInt("uSpotLightCount", m_localLights.SpotCount);
    for (int i = 0; i < m_localLights.SpotCount; ++i)
    {
        const SpotLight& light = *m_localLights.Spots[i];
        const std::string base = "uSpotLights[" + std::to_string(i) + "].";
        m_pbrShader->SetVec3(base + "Position", light.Position);
        m_pbrShader->SetVec3(base + "Direction", m_localLights.SpotDirections[i]);
        m_pbrShader->SetVec3(base + "Color", light.Color * light.Intensity);
        m_pbrShader->SetFloat(base + "Range", light.Range);
        m_pbrShader->SetFloat(base + "CosInner", std::cos(glm::radians(light.InnerConeDeg)));
        m_pbrShader->SetFloat(base + "CosOuter", std::cos(glm::radians(light.OuterConeDeg)));
        m_pbrShader->SetMat4(base + "Matrix", m_localLights.SpotMatrices[i]);
        m_pbrShader->SetVec4(base + "ShadowRect", m_localLights.SpotShadowRects[i]);
        m_pbrShader->SetInt(base + "CookieIndex", m_localLights.SpotCookieSlots[i]);
    }

    m_pbrShader->SetInt("uAreaLightCount", m_localLights.AreaCount);
    for (int i = 0; i < m_localLights.AreaCount; ++i)
    {
        const AreaLight& light = *m_localLights.Areas[i];
        const glm::vec3& direction = m_localLights.AreaDirections[i];
        const glm::vec3& right = m_localLights.AreaRights[i];
        const glm::vec3& up = m_localLights.AreaUps[i];
        const std::string base = "uAreaLights[" + std::to_string(i) + "].";
        m_pbrShader->SetVec3(base + "Position", light.Position);
        m_pbrShader->SetVec3(base + "Direction", direction);
        m_pbrShader->SetVec3(base + "Right", right);
        m_pbrShader->SetVec3(base + "Up", up);
        m_pbrShader->SetVec3(base + "Color", light.Color * light.Intensity);
        m_pbrShader->SetVec2(base + "HalfSize", light.Size * 0.5f);
        m_pbrShader->SetVec2(base + "Softness", light.Softness);
        m_pbrShader->SetFloat(base + "Range", light.Range);
        m_pbrShader->SetFloat(base + "MinRoughness", light.MinRoughness);
        m_pbrShader->SetMat4(base + "Matrix", m_localLights.AreaMatrices[i]);
        m_pbrShader->SetVec4(base + "ShadowRect", m_localLights.AreaShadowRects[i]);
        m_pbrShader->SetInt(base + "CookieIndex", m_localLights.AreaCookieSlots[i]);
    }
}

} // namespace engine
