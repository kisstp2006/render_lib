#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/core/Log.h"
#include "engine/core/Window.h"
#include "engine/debug/DebugOverlay.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace engine {

namespace {

void APIENTRY GLDebugCallback(GLenum, GLenum type, unsigned int, GLenum severity,
                              GLsizei, const char* message, const void*)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return;
    (type == GL_DEBUG_TYPE_ERROR ? log::Error : log::Warn)(std::string("[GL] ") + message);
}

} // namespace

void GLRenderBackend::Init(Window& window, const RenderBackendConfig& config)
{
    m_window = &window;
    m_width = window.Width();
    m_height = window.Height();
    glfwMakeContextCurrent(window.Handle());
    if (!gladLoadGL(glfwGetProcAddress))
        throw std::runtime_error("Failed to initialize GLAD/OpenGL");

    int maximumSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maximumSamples);
    m_capabilities.AdapterName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    m_capabilities.MaxMsaaSamples = static_cast<uint32_t>(std::max(maximumSamples, 1));
    m_capabilities.GpuTiming = true;
    m_capabilities.ImmediatePresent = true;
    m_capabilities.AdaptivePresent = glfwExtensionSupported("WGL_EXT_swap_control_tear")
        || glfwExtensionSupported("GLX_EXT_swap_control_tear");
#ifdef GL_MAX_TEXTURE_MAX_ANISOTROPY
    float deviceAnisotropy = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &deviceAnisotropy);
    m_capabilities.MaxAnisotropy = std::max(deviceAnisotropy, 1.0f);
#endif
    const uint32_t requestedSamples = std::max(config.MsaaSamples, 1u);
    m_msaaSamples = 1;
    for (const int samples : {2, 4, 8, 16})
        if (static_cast<uint32_t>(samples) <= requestedSamples && samples <= maximumSamples)
            m_msaaSamples = samples;
    m_maxAnisotropy = std::clamp(config.MaxAnisotropy, 1.0f,
                                 m_capabilities.MaxAnisotropy);
    m_capabilities.ActiveMsaaSamples = static_cast<uint32_t>(m_msaaSamples);
    m_capabilities.ActiveAnisotropy = m_maxAnisotropy;
    m_gpuTimingEnabled = config.EnableGpuTiming;
    SetPresentMode(config.Presentation);

    if (config.EnableValidation && GLAD_GL_KHR_debug)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(GLDebugCallback, nullptr);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const std::string shaderDir = ENGINE_SHADER_DIR;
    const std::string fullscreen = shaderDir + "/gl/common/fullscreen.vert";
    m_pbrShader = std::make_unique<GLShader>(shaderDir + "/gl/lighting/pbr.vert", shaderDir + "/gl/lighting/pbr.frag");
    m_shadowShader = std::make_unique<GLShader>(shaderDir + "/gl/lighting/shadow.vert", shaderDir + "/gl/lighting/shadow.frag");
    m_pointShadowShader = std::make_unique<GLShader>(shaderDir + "/gl/lighting/point_shadow.vert", shaderDir + "/gl/lighting/point_shadow.frag");
    m_skyShader = std::make_unique<GLShader>(shaderDir + "/gl/environment/sky.vert", shaderDir + "/gl/environment/sky.frag");
    m_bloomDownShader = std::make_unique<GLShader>(fullscreen, shaderDir + "/gl/post/bloom_downsample.frag");
    m_bloomUpShader = std::make_unique<GLShader>(fullscreen, shaderDir + "/gl/post/bloom_upsample.frag");
    m_taaShader = std::make_unique<GLShader>(fullscreen, shaderDir + "/gl/post/taa_resolve.frag");
    m_fxaaShader = std::make_unique<GLShader>(fullscreen, shaderDir + "/gl/post/fxaa.frag");
    m_postShader = std::make_unique<GLShader>(fullscreen, shaderDir + "/gl/post/post.frag");
    m_debugOverlayShader = std::make_unique<GLShader>(fullscreen, shaderDir + "/gl/debug/overlay.frag");
    m_environment = std::make_unique<GLEnvironment>(shaderDir);

    glGenVertexArrays(1, &m_emptyVao);
    const auto white = textures::MakeSolidColor({1.0f, 1.0f, 1.0f, 1.0f}, false);
    const auto flatNormal = textures::MakeFlatNormal();
    m_defaultWhite = std::make_unique<GLTexture>(*white, m_maxAnisotropy);
    m_defaultNormal = std::make_unique<GLTexture>(*flatNormal, m_maxAnisotropy);
    glGenTextures(1, &m_debugOverlayTexture);
    glBindTexture(GL_TEXTURE_2D, m_debugOverlayTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, debug::DebugOverlayImage::Width,
                 debug::DebugOverlayImage::Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    InitShadowMap();
    InitLocalLightResources();
    CreateSceneTargets(m_width, m_height);

    glGenBuffers(2, m_exposurePbos);
    for (unsigned int pbo : m_exposurePbos)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, sizeof(float) * 4, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (m_gpuTimingEnabled)
    {
        glGenQueries(2, m_shadowTimeQueries);
        glGenQueries(2, m_localShadowTimeQueries);
        glGenQueries(2, m_postTimeQueries);
        glGenQueries(2, m_mainTimeQueries);
    }
    log::Info("GL renderer initialized (HDR + MSAA + IBL + bloom)");
}

bool GLRenderBackend::SetPresentMode(PresentMode mode)
{
    m_presentMode = mode;
    if (!m_window)
        return false;
    if (mode == PresentMode::Immediate)
        m_window->SetSwapInterval(0);
    else if (mode == PresentMode::Adaptive && m_capabilities.AdaptivePresent)
        m_window->SetSwapInterval(-1);
    else
        m_window->SetSwapInterval(1);
    return mode != PresentMode::Adaptive || m_capabilities.AdaptivePresent;
}

void GLRenderBackend::Shutdown()
{
    for (const auto& [data, texture] : m_colorLutCache)
        glDeleteTextures(1, &texture);
    m_colorLutCache.clear();
    m_meshCache.clear();
    m_textureCache.clear();
    m_defaultWhite.reset();
    m_defaultNormal.reset();
    m_environment.reset();
    m_pbrShader.reset();
    m_shadowShader.reset();
    m_pointShadowShader.reset();
    m_skyShader.reset();
    m_bloomDownShader.reset();
    m_bloomUpShader.reset();
    m_taaShader.reset();
    m_fxaaShader.reset();
    m_postShader.reset();
    m_debugOverlayShader.reset();
    DestroySceneTargets();
    DestroyLocalLightResources();

    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
    if (m_debugOverlayTexture) glDeleteTextures(1, &m_debugOverlayTexture);
    m_debugOverlayTexture = 0;
    glDeleteTextures(kShadowCascadeCount, m_shadowMaps.data());
    if (m_shadowFbo) glDeleteFramebuffers(1, &m_shadowFbo);
    glDeleteBuffers(2, m_exposurePbos);
    glDeleteQueries(2, m_shadowTimeQueries);
    glDeleteQueries(2, m_localShadowTimeQueries);
    glDeleteQueries(2, m_postTimeQueries);
    glDeleteQueries(2, m_mainTimeQueries);
    m_exposurePbos[0] = m_exposurePbos[1] = 0;
}

void GLRenderBackend::Resize(int width, int height)
{
    m_width = width;
    m_height = height;
    CreateSceneTargets(width, height);
}

GLMesh& GLRenderBackend::GetOrCreateMesh(const std::shared_ptr<MeshData>& data)
{
    if (const auto found = m_meshCache.find(data); found != m_meshCache.end())
        return *found->second;
    auto mesh = std::make_unique<GLMesh>(*data);
    GLMesh& result = *mesh;
    m_meshCache.emplace(data, std::move(mesh));
    return result;
}

GLTexture& GLRenderBackend::GetOrCreateTexture(const std::shared_ptr<TextureData>& data)
{
    if (const auto found = m_textureCache.find(data); found != m_textureCache.end())
        return *found->second;
    auto texture = std::make_unique<GLTexture>(*data, m_maxAnisotropy);
    GLTexture& result = *texture;
    m_textureCache.emplace(data, std::move(texture));
    return result;
}

void GLRenderBackend::BindMaterialTexture(const std::shared_ptr<TextureData>& map, int unit, GLTexture& fallback)
{
    if (map)
        GetOrCreateTexture(map).Bind(unit);
    else
        fallback.Bind(unit);
}

} // namespace engine
