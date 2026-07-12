#include "engine/backend/gl/GLRenderBackend.h"
#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/core/Window.h"
#include "engine/scene/Scene.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {

namespace {

constexpr int kBloomLevels = 6;
constexpr int kUnitShadow = 0;
constexpr int kUnitAlbedo = 1;
constexpr int kUnitNormal = 2;
constexpr int kUnitMrao = 3;
constexpr int kUnitIrradiance = 4;
constexpr int kUnitPrefilter = 5;
constexpr int kUnitBrdfLut = 6;
constexpr int kUnitSpotShadow = 7;

void APIENTRY GLDebugCallback(GLenum /*source*/, GLenum type, unsigned int /*id*/, GLenum severity,
                              GLsizei /*length*/, const char* message, const void* /*userParam*/)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return;

    const bool isError = (type == GL_DEBUG_TYPE_ERROR);
    (isError ? log::Error : log::Warn)(std::string("[GL] ") + message);
}

// Same curve as post.frag's TonemapColor, used to precompute the white
// point scale on the CPU exactly like VRF does (g_flWhitePointScale).
float TonemapScalar(float x, const PostProcessSettings& pp)
{
    const float num = x * (pp.ShoulderStrength * x + pp.LinearStrength * pp.LinearAngle) + pp.ToeNumerator * pp.ToeStrength;
    const float den = x * (pp.ShoulderStrength * x + pp.LinearStrength) + pp.ToeDenominator * pp.ToeStrength;
    return num / den - pp.ToeNumerator / pp.ToeDenominator;
}

} // namespace

void GLRenderBackend::Init(Window& window)
{
    m_window = &window;
    m_width = window.Width();
    m_height = window.Height();

    glfwMakeContextCurrent(window.Handle());

    if (!gladLoadGL(glfwGetProcAddress))
        throw std::runtime_error("Failed to initialize GLAD/OpenGL");

#ifdef GL_DEBUG_OUTPUT
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(GLDebugCallback, nullptr);
#endif

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const std::string shaderDir = ENGINE_SHADER_DIR;
    m_pbrShader = std::make_unique<GLShader>(shaderDir + "/gl/pbr.vert", shaderDir + "/gl/pbr.frag");
    m_shadowShader = std::make_unique<GLShader>(shaderDir + "/gl/shadow.vert", shaderDir + "/gl/shadow.frag");
    m_skyShader = std::make_unique<GLShader>(shaderDir + "/gl/sky.vert", shaderDir + "/gl/sky.frag");
    m_bloomDownShader = std::make_unique<GLShader>(shaderDir + "/gl/fullscreen.vert", shaderDir + "/gl/bloom_downsample.frag");
    m_bloomUpShader = std::make_unique<GLShader>(shaderDir + "/gl/fullscreen.vert", shaderDir + "/gl/bloom_upsample.frag");
    m_postShader = std::make_unique<GLShader>(shaderDir + "/gl/fullscreen.vert", shaderDir + "/gl/post.frag");

    m_environment = std::make_unique<GLEnvironment>(shaderDir);

    glGenVertexArrays(1, &m_emptyVao);

    auto white = textures::MakeSolidColor({1.0f, 1.0f, 1.0f, 1.0f}, false);
    auto flatNormal = textures::MakeFlatNormal();
    m_defaultWhite = std::make_unique<GLTexture>(*white);
    m_defaultNormal = std::make_unique<GLTexture>(*flatNormal);

    InitShadowMap();
    CreateSceneTargets(m_width, m_height);

    log::Info("GL renderer initialized (HDR + MSAA + IBL + bloom)");
}

namespace {

// Depth-only FBO for shadow rendering. Manual PCF is done in pbr.frag
// against a plain sampler2D, so these stay regular (non-shadow) samplers -
// GL_TEXTURE_COMPARE_MODE would force sampler2DShadow semantics and produce
// undefined results.
void CreateDepthTarget(unsigned int& fbo, unsigned int& texture, int size)
{
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Shadow map framebuffer incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace

void GLRenderBackend::InitShadowMap()
{
    CreateDepthTarget(m_shadowFbo, m_shadowMap, m_shadowSize);
    CreateDepthTarget(m_spotShadowFbo, m_spotShadowMap, m_spotShadowSize);
}

void GLRenderBackend::CreateSceneTargets(int width, int height)
{
    DestroySceneTargets();

    // MSAA HDR target
    glGenRenderbuffers(1, &m_msaaColorRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_msaaSamples, GL_RGBA16F, width, height);

    glGenRenderbuffers(1, &m_msaaDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_msaaSamples, GL_DEPTH_COMPONENT32F, width, height);

    glGenFramebuffers(1, &m_msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColorRbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("MSAA HDR framebuffer incomplete");

    // Resolve target (plain HDR texture the bloom/post passes sample)
    glGenTextures(1, &m_hdrColorTex);
    glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hdrColorTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Resolve framebuffer incomplete");

    // Bloom chain: half res downward
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
}

void GLRenderBackend::DestroySceneTargets()
{
    for (auto& level : m_bloomChain)
    {
        glDeleteFramebuffers(1, &level.Fbo);
        glDeleteTextures(1, &level.Texture);
    }
    m_bloomChain.clear();

    if (m_resolveFbo) { glDeleteFramebuffers(1, &m_resolveFbo); m_resolveFbo = 0; }
    if (m_hdrColorTex) { glDeleteTextures(1, &m_hdrColorTex); m_hdrColorTex = 0; }
    if (m_msaaFbo) { glDeleteFramebuffers(1, &m_msaaFbo); m_msaaFbo = 0; }
    if (m_msaaColorRbo) { glDeleteRenderbuffers(1, &m_msaaColorRbo); m_msaaColorRbo = 0; }
    if (m_msaaDepthRbo) { glDeleteRenderbuffers(1, &m_msaaDepthRbo); m_msaaDepthRbo = 0; }
}

void GLRenderBackend::Shutdown()
{
    m_meshCache.clear();
    m_textureCache.clear();
    m_defaultWhite.reset();
    m_defaultNormal.reset();
    m_environment.reset();
    m_pbrShader.reset();
    m_shadowShader.reset();
    m_skyShader.reset();
    m_bloomDownShader.reset();
    m_bloomUpShader.reset();
    m_postShader.reset();

    DestroySceneTargets();

    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
    if (m_shadowMap) glDeleteTextures(1, &m_shadowMap);
    if (m_shadowFbo) glDeleteFramebuffers(1, &m_shadowFbo);
    if (m_spotShadowMap) glDeleteTextures(1, &m_spotShadowMap);
    if (m_spotShadowFbo) glDeleteFramebuffers(1, &m_spotShadowFbo);
}

void GLRenderBackend::Resize(int width, int height)
{
    m_width = width;
    m_height = height;
    CreateSceneTargets(width, height);
}

GLMesh& GLRenderBackend::GetOrCreateMesh(const MeshData& data)
{
    auto it = m_meshCache.find(&data);
    if (it != m_meshCache.end())
        return *it->second;

    auto mesh = std::make_unique<GLMesh>(data);
    GLMesh& ref = *mesh;
    m_meshCache.emplace(&data, std::move(mesh));
    return ref;
}

GLTexture& GLRenderBackend::GetOrCreateTexture(const TextureData& data)
{
    auto it = m_textureCache.find(&data);
    if (it != m_textureCache.end())
        return *it->second;

    auto tex = std::make_unique<GLTexture>(data);
    GLTexture& ref = *tex;
    m_textureCache.emplace(&data, std::move(tex));
    return ref;
}

void GLRenderBackend::BindMaterialTexture(const std::shared_ptr<TextureData>& map, int unit, GLTexture& fallback)
{
    if (map)
        GetOrCreateTexture(*map).Bind(unit);
    else
        fallback.Bind(unit);
}

void GLRenderBackend::RenderFrame(const Scene& scene, const Camera& camera)
{
    m_environment->EnsureBaked(scene.Sun, scene.Sky);

    // Light-space matrix for the sun: fixed-size ortho box following the
    // light direction, centered on the world origin. Fine for a bounded demo
    // scene; a cascaded/scene-fitted version is the natural follow-up.
    const glm::vec3 lightDir = glm::normalize(scene.Sun.Direction);
    const float orthoSize = 25.0f;
    const glm::vec3 lightPos = -lightDir * 40.0f;
    const glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 1.0f, 100.0f);
    const glm::mat4 lightSpaceMatrix = lightProj * lightView;

    // --- Shadow pass ---
    glViewport(0, 0, m_shadowSize, m_shadowSize);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glClear(GL_DEPTH_BUFFER_BIT);
    glCullFace(GL_FRONT); // reduce peter-panning / shadow acne on thin geometry

    m_shadowShader->Use();
    m_shadowShader->SetMat4("uLightSpaceMatrix", lightSpaceMatrix);

    for (const auto& instance : scene.Instances())
    {
        m_shadowShader->SetMat4("uModel", instance.Transform);
        GetOrCreateMesh(*instance.Mesh).Draw();
    }

    glCullFace(GL_BACK);

    // --- Spot (flashlight) shadow pass ---
    // The first enabled, shadow-casting spot among the first 4 gets the map.
    const auto& spots = scene.SpotLights();
    std::vector<const SpotLight*> activeSpots;
    for (const auto& spot : spots)
    {
        if (spot.Enabled && activeSpots.size() < 4)
            activeSpots.push_back(&spot);
    }

    int spotShadowIndex = -1;
    glm::mat4 spotShadowMatrix{1.0f};
    for (size_t i = 0; i < activeSpots.size(); ++i)
    {
        if (activeSpots[i]->CastsShadows)
        {
            spotShadowIndex = static_cast<int>(i);
            break;
        }
    }

    if (spotShadowIndex >= 0)
    {
        const SpotLight& spot = *activeSpots[spotShadowIndex];
        const glm::vec3 dir = glm::normalize(spot.Direction);
        const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::mat4 spotView = glm::lookAt(spot.Position, spot.Position + dir, up);
        const glm::mat4 spotProj = glm::perspective(glm::radians(spot.OuterConeDeg * 2.0f), 1.0f, 0.1f, spot.Range);
        spotShadowMatrix = spotProj * spotView;

        glViewport(0, 0, m_spotShadowSize, m_spotShadowSize);
        glBindFramebuffer(GL_FRAMEBUFFER, m_spotShadowFbo);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);

        m_shadowShader->Use();
        m_shadowShader->SetMat4("uLightSpaceMatrix", spotShadowMatrix);
        for (const auto& instance : scene.Instances())
        {
            m_shadowShader->SetMat4("uModel", instance.Transform);
            GetOrCreateMesh(*instance.Mesh).Draw();
        }

        glCullFace(GL_BACK);
    }

    // --- Main HDR pass (MSAA) ---
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = m_height > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f;
    const glm::mat4 view = camera.GetView();
    const glm::mat4 proj = camera.GetProjection(aspect);

    m_pbrShader->Use();
    m_pbrShader->SetMat4("uView", view);
    m_pbrShader->SetMat4("uProj", proj);
    m_pbrShader->SetMat4("uLightSpaceMatrix", lightSpaceMatrix);
    m_pbrShader->SetVec3("uCameraPos", camera.Position);

    m_pbrShader->SetVec3("uSunDirection", lightDir);
    m_pbrShader->SetVec3("uSunColor", scene.Sun.Color * scene.Sun.Intensity);

    const auto& lights = scene.PointLights();
    const int lightCount = static_cast<int>(std::min<size_t>(lights.size(), 8));
    m_pbrShader->SetInt("uPointLightCount", lightCount);
    for (int i = 0; i < lightCount; ++i)
    {
        const std::string base = "uPointLights[" + std::to_string(i) + "].";
        m_pbrShader->SetVec3(base + "Position", lights[i].Position);
        m_pbrShader->SetVec3(base + "Color", lights[i].Color * lights[i].Intensity);
        m_pbrShader->SetFloat(base + "Radius", lights[i].Radius);
    }

    m_pbrShader->SetInt("uSpotLightCount", static_cast<int>(activeSpots.size()));
    for (size_t i = 0; i < activeSpots.size(); ++i)
    {
        const SpotLight& spot = *activeSpots[i];
        const std::string base = "uSpotLights[" + std::to_string(i) + "].";
        m_pbrShader->SetVec3(base + "Position", spot.Position);
        m_pbrShader->SetVec3(base + "Direction", glm::normalize(spot.Direction));
        m_pbrShader->SetVec3(base + "Color", spot.Color * spot.Intensity);
        m_pbrShader->SetFloat(base + "Range", spot.Range);
        m_pbrShader->SetFloat(base + "CosInner", std::cos(glm::radians(spot.InnerConeDeg)));
        m_pbrShader->SetFloat(base + "CosOuter", std::cos(glm::radians(spot.OuterConeDeg)));
    }

    m_pbrShader->SetInt("uSpotShadowIndex", spotShadowIndex);
    m_pbrShader->SetMat4("uSpotShadowMatrix", spotShadowMatrix);
    glActiveTexture(GL_TEXTURE0 + kUnitSpotShadow);
    glBindTexture(GL_TEXTURE_2D, m_spotShadowMap);
    m_pbrShader->SetInt("uSpotShadowMap", kUnitSpotShadow);

    const FogSettings& fog = scene.Fog;
    m_pbrShader->SetBool("uFogEnabled", fog.Enabled);
    m_pbrShader->SetVec3("uFogColor", fog.Color);
    m_pbrShader->SetFloat("uFogOpacity", fog.Opacity);
    m_pbrShader->SetVec2("uFogStartEnd", {fog.Start, fog.End});
    m_pbrShader->SetFloat("uFogDistanceExponent", fog.DistanceExponent);
    m_pbrShader->SetVec2("uFogHeightTopBottom", {fog.HeightFadeTop, fog.HeightFadeBottom});
    m_pbrShader->SetFloat("uFogHeightExponent", fog.HeightExponent);

    glActiveTexture(GL_TEXTURE0 + kUnitShadow);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    m_pbrShader->SetInt("uShadowMap", kUnitShadow);

    m_environment->BindIrradianceMap(kUnitIrradiance);
    m_environment->BindPrefilterMap(kUnitPrefilter);
    m_environment->BindBrdfLut(kUnitBrdfLut);
    m_pbrShader->SetInt("uIrradianceMap", kUnitIrradiance);
    m_pbrShader->SetInt("uPrefilterMap", kUnitPrefilter);
    m_pbrShader->SetInt("uBrdfLut", kUnitBrdfLut);
    m_pbrShader->SetFloat("uPrefilterMips", static_cast<float>(GLEnvironment::kPrefilterMips));

    m_pbrShader->SetInt("uAlbedoMap", kUnitAlbedo);
    m_pbrShader->SetInt("uNormalMap", kUnitNormal);
    m_pbrShader->SetInt("uMraoMap", kUnitMrao);

    for (const auto& instance : scene.Instances())
    {
        m_pbrShader->SetMat4("uModel", instance.Transform);
        m_pbrShader->SetMat4("uNormalMatrix", glm::transpose(glm::inverse(instance.Transform)));

        const Material& mat = instance.Mat;
        m_pbrShader->SetVec3("uAlbedo", mat.Albedo);
        m_pbrShader->SetFloat("uMetallic", mat.Metallic);
        m_pbrShader->SetFloat("uRoughness", mat.Roughness);
        m_pbrShader->SetVec3("uEmissive", mat.Emissive);
        m_pbrShader->SetFloat("uAO", mat.AmbientOcclusion);
        m_pbrShader->SetFloat("uSpecularF0", mat.SpecularF0);

        m_pbrShader->SetBool("uHasAlbedoMap", mat.AlbedoMap != nullptr);
        m_pbrShader->SetBool("uHasNormalMap", mat.NormalMap != nullptr);
        m_pbrShader->SetBool("uHasMraoMap", mat.MraoMap != nullptr);
        BindMaterialTexture(mat.AlbedoMap, kUnitAlbedo, *m_defaultWhite);
        BindMaterialTexture(mat.NormalMap, kUnitNormal, *m_defaultNormal);
        BindMaterialTexture(mat.MraoMap, kUnitMrao, *m_defaultWhite);

        GetOrCreateMesh(*instance.Mesh).Draw();
    }

    // --- Skybox (only fills pixels the geometry left at depth 1.0) ---
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    m_skyShader->Use();
    m_skyShader->SetMat4("uInvProj", glm::inverse(proj));
    m_skyShader->SetMat4("uInvView", glm::inverse(view));
    m_environment->BindEnvironmentMap(0);
    m_skyShader->SetInt("uEnvMap", 0);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // --- Resolve MSAA -> HDR texture ---
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // --- Auto-exposure (Source 2 tonemap controller style) ---
    // Average scene luminance from the top mip of the HDR resolve texture,
    // adapted over time toward Key/avgLum within [Min, Max] multipliers.
    // The 1x1 readback is a sync point; acceptable until a GPU histogram
    // replaces it.
    const PostProcessSettings& pp = scene.PostProcess;
    const double now = glfwGetTime();
    const float deltaTime = m_lastFrameTime > 0.0 ? static_cast<float>(now - m_lastFrameTime) : 0.016f;
    m_lastFrameTime = now;

    if (pp.Enabled && pp.AutoExposure)
    {
        glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
        glGenerateMipmap(GL_TEXTURE_2D);

        const int maxDim = std::max(m_width, m_height);
        const int topMip = static_cast<int>(std::floor(std::log2(static_cast<float>(maxDim))));

        float avg[4] = {0, 0, 0, 0};
        glGetTexImage(GL_TEXTURE_2D, topMip, GL_RGBA, GL_FLOAT, avg);
        const float avgLum = std::max(0.2126f * avg[0] + 0.7152f * avg[1] + 0.0722f * avg[2], 1e-4f);

        const float target = std::clamp(pp.AutoExposureKey / avgLum, pp.AutoExposureMin, pp.AutoExposureMax);
        const float blend = 1.0f - std::exp(-deltaTime * pp.AutoExposureSpeed);
        m_autoExposure += (target - m_autoExposure) * blend;
    }
    else
    {
        m_autoExposure = 1.0f;
    }

    const float effectiveExposure = pp.Exposure * m_autoExposure;

    // --- Bloom ---
    if (pp.Enabled && !m_bloomChain.empty())
        RenderBloom(pp.BloomThreshold, effectiveExposure);

    // --- Post pass to backbuffer ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);

    m_postShader->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
    m_postShader->SetInt("uSceneColor", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (pp.Enabled && !m_bloomChain.empty()) ? m_bloomChain[0].Texture : m_defaultWhite->Id());
    m_postShader->SetInt("uBloom", 1);

    m_postShader->SetBool("uPostEnabled", pp.Enabled);
    m_postShader->SetFloat("uExposure", effectiveExposure);
    m_postShader->SetFloat("uSaturation", pp.Saturation);
    m_postShader->SetFloat("uContrast", pp.Contrast);
    m_postShader->SetVec3("uColorTint", pp.ColorTint);
    m_postShader->SetFloat("uBloomStrength", (pp.Enabled && !m_bloomChain.empty()) ? pp.BloomStrength : 0.0f);
    m_postShader->SetFloat("uShoulderStrength", pp.ShoulderStrength);
    m_postShader->SetFloat("uLinearStrength", pp.LinearStrength);
    m_postShader->SetFloat("uLinearAngle", pp.LinearAngle);
    m_postShader->SetFloat("uToeStrength", pp.ToeStrength);
    m_postShader->SetFloat("uToeNumerator", pp.ToeNumerator);
    m_postShader->SetFloat("uToeDenominator", pp.ToeDenominator);
    m_postShader->SetFloat("uWhitePointScale", 1.0f / TonemapScalar(pp.WhitePoint, pp));

    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);

    if (!m_screenshotPath.empty())
        SaveScreenshot();
}

void GLRenderBackend::RenderBloom(float threshold, float exposure)
{
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_emptyVao);

    // Downsample chain: hdr -> level0 -> level1 -> ...
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

    // Upsample chain with additive blending: levelN adds into levelN-1
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
