#include "engine/backend/gl/GLRenderBackend.h"
#include "engine/backend/gl/GLDebug.h"

#include "engine/core/Log.h"
#include "engine/core/Window.h"
#include "engine/debug/DebugOverlay.h"
#include "engine/render/TextureFallback.h"

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
    const auto stringValue = [](GLenum name) -> std::string
    {
        const GLubyte* value = glGetString(name);
        return value ? reinterpret_cast<const char*>(value) : "Unknown";
    };
    GpuRawCapabilities raw;
    raw.Device.Api = GpuApi::OpenGL;
    raw.Device.DeviceName = stringValue(GL_RENDERER);
    raw.Device.VendorName = stringValue(GL_VENDOR);
    raw.Device.ApiVersion = stringValue(GL_VERSION);
    raw.Device.DriverName = raw.Device.VendorName + " OpenGL driver";
    raw.Device.DriverInfo = raw.Device.ApiVersion + "; GLSL " + stringValue(GL_SHADING_LANGUAGE_VERSION);
    raw.Device.Vendor = IdentifyGpuVendor(0, raw.Device.VendorName, raw.Device.DeviceName);
    raw.MaxMsaaSamples = static_cast<uint32_t>(std::max(maximumSamples, 1));
    raw.GpuTimestamps = GLAD_GL_VERSION_3_3 || GLAD_GL_ARB_timer_query;
    raw.TextureCompressionBc = GLAD_GL_EXT_texture_compression_s3tc &&
        (GLAD_GL_VERSION_3_0 || GLAD_GL_ARB_texture_compression_rgtc);
    raw.TextureCompressionBc7 = GLAD_GL_VERSION_4_2 || GLAD_GL_ARB_texture_compression_bptc;
    raw.TextureCompressionAstc = GLAD_GL_KHR_texture_compression_astc_ldr != 0;
    m_gpuPipelineStatisticsSupported = GLAD_GL_ARB_pipeline_statistics_query != 0;
    m_gpuMemoryBudgetSupported = GLAD_GL_NVX_gpu_memory_info != 0;
    raw.PipelineStatistics = m_gpuPipelineStatisticsSupported;
    raw.MemoryBudget = m_gpuMemoryBudgetSupported;
    raw.ImmediatePresent = true;
    raw.AdaptivePresent = glfwExtensionSupported("WGL_EXT_swap_control_tear")
        || glfwExtensionSupported("GLX_EXT_swap_control_tear");
    m_capabilities.AdapterName = raw.Device.DeviceName;
    m_capabilities.MaxMsaaSamples = raw.MaxMsaaSamples;
    m_capabilities.GpuTiming = raw.GpuTimestamps;
    m_capabilities.GpuPipelineStatistics = raw.PipelineStatistics;
    m_capabilities.GpuMemoryBudget = raw.MemoryBudget;
    m_capabilities.ImmediatePresent = raw.ImmediatePresent;
    m_capabilities.AdaptivePresent = raw.AdaptivePresent;
    if (m_gpuMemoryBudgetSupported)
    {
        constexpr GLenum kGpuMemoryInfoTotalAvailableMemoryNvx = 0x9048;
        GLint dedicatedVideoMemoryKilobytes = 0;
        glGetIntegerv(kGpuMemoryInfoTotalAvailableMemoryNvx,
                      &dedicatedVideoMemoryKilobytes);
        if (dedicatedVideoMemoryKilobytes > 0)
            m_capabilities.DedicatedVideoMemoryBytes =
                static_cast<uint64_t>(dedicatedVideoMemoryKilobytes) * 1024ull;
    }
#ifdef GL_MAX_TEXTURE_MAX_ANISOTROPY
    if (GLAD_GL_EXT_texture_filter_anisotropic)
    {
        float deviceAnisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &deviceAnisotropy);
        m_capabilities.MaxAnisotropy = std::max(deviceAnisotropy, 1.0f);
    }
#endif
    raw.MaxAnisotropy = m_capabilities.MaxAnisotropy;
    GpuCapabilityRequest request;
    request.MsaaSamples = config.MsaaSamples;
    request.MaxAnisotropy = config.MaxAnisotropy;
    request.EnableGpuTiming = config.EnableGpuTiming;
    request.RequestImmediatePresent = config.Presentation == PresentMode::Immediate;
    request.RequestAdaptivePresent = config.Presentation == PresentMode::Adaptive;
    request.Policy = config.CapabilityPolicy;
    request.EnableDriverWorkarounds = config.EnableDriverWorkarounds;
    m_capabilities.Gpu = EvaluateGpuCapabilities(raw, request);
    m_msaaSamples = static_cast<int>(m_capabilities.Gpu.SelectedMsaaSamples);
    m_maxAnisotropy = m_capabilities.Gpu.SelectedAnisotropy;
    m_capabilities.ActiveMsaaSamples = static_cast<uint32_t>(m_msaaSamples);
    m_capabilities.ActiveAnisotropy = m_maxAnisotropy;
    m_gpuTimingEnabled = m_capabilities.Gpu.Uses(GpuFeature::GpuTimestamps);
    SetPresentMode(config.Presentation);

    log::Info("OpenGL GPU: " + raw.Device.DeviceName + " (" + raw.Device.VendorName +
              ", " + raw.Device.ApiVersion + ", tier " +
              GpuFeatureTierName(m_capabilities.Gpu.Tier) + ")");
    for (const GpuFallbackDecision& fallback : m_capabilities.Gpu.Fallbacks)
        log::Warn("GPU fallback [" + fallback.RuleId + "] " + fallback.Feature + ": " +
                  fallback.Requested + " -> " + fallback.Selected + " (" + fallback.Reason + ")");

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
    glBindVertexArray(m_emptyVao);
    gl_debug::LabelObject(GL_VERTEX_ARRAY, m_emptyVao, "Fullscreen Triangle VAO");
    glBindVertexArray(0);
    const auto white = textures::MakeSolidColor({1.0f, 1.0f, 1.0f, 1.0f}, false);
    const auto flatNormal = textures::MakeFlatNormal();
    m_defaultWhite = std::make_unique<GLTexture>(*white, m_maxAnisotropy);
    m_defaultNormal = std::make_unique<GLTexture>(*flatNormal, m_maxAnisotropy);
    glGenTextures(static_cast<GLsizei>(m_debugOverlayTextures.size()),
                  m_debugOverlayTextures.data());
    for (size_t index = 0; index < m_debugOverlayTextures.size(); ++index)
    {
        glBindTexture(GL_TEXTURE_2D, m_debugOverlayTextures[index]);
        gl_debug::LabelObject(GL_TEXTURE, m_debugOverlayTextures[index],
            "Debug UI Atlas " + std::to_string(index));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, debug::DebugOverlayImage::Width,
                     debug::DebugOverlayImage::Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    InitShadowMap();
    InitLocalLightResources();
    CreateSceneTargets(m_width, m_height);

    glGenBuffers(2, m_exposurePbos);
    for (int index = 0; index < 2; ++index)
    {
        const unsigned int pbo = m_exposurePbos[index];
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        gl_debug::LabelObject(GL_BUFFER, pbo,
            "Auto Exposure Readback " + std::to_string(index));
        glBufferData(GL_PIXEL_PACK_BUFFER, sizeof(float) * 4, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    CreateGpuProfilerQueries();
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
    m_textureFallbackWarnings.clear();
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
    glDeleteTextures(static_cast<GLsizei>(m_debugOverlayTextures.size()),
                     m_debugOverlayTextures.data());
    m_debugOverlayTextures.fill(0);
    glDeleteTextures(kShadowCascadeCount, m_shadowMaps.data());
    if (m_shadowFbo) glDeleteFramebuffers(1, &m_shadowFbo);
    glDeleteBuffers(2, m_exposurePbos);
    DestroyGpuProfilerQueries();
    m_exposurePbos[0] = m_exposurePbos[1] = 0;
    m_emptyVao = 0;
    m_shadowMaps.fill(0);
    m_shadowFbo = 0;
    m_width = 0;
    m_height = 0;
    m_window = nullptr;
}

void GLRenderBackend::Resize(int width, int height)
{
    m_width = width;
    m_height = height;
    m_hasFrameDebugFrame = false;
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
    TextureData scratch;
    std::string reason;
    const TextureData* upload = ResolveTextureForGpu(*data, m_capabilities.Gpu, scratch, &reason);
    if (!upload)
    {
        if (m_textureFallbackWarnings.insert(data.get()).second)
        {
            log::Warn("OpenGL texture fallback: " + reason + "; using the default texture");
            m_capabilities.Gpu.Fallbacks.push_back(
                {"unsupported-texture-format", TextureStorageName(data->Storage),
                 "native upload", "material default texture", reason});
        }
        return *m_defaultWhite;
    }
    if (upload != data.get() && m_textureFallbackWarnings.insert(data.get()).second)
    {
        log::Warn("OpenGL texture fallback: " + reason);
        const std::string feature = TextureStorageName(data->Storage);
        if (std::none_of(m_capabilities.Gpu.Fallbacks.begin(), m_capabilities.Gpu.Fallbacks.end(),
                         [&feature](const GpuFallbackDecision& fallback)
                         { return fallback.Feature == feature; }))
            m_capabilities.Gpu.Fallbacks.push_back(
                {"unsupported-texture-format", feature, "native upload",
                 "RGBA8 fallback mip chain", reason});
    }
    auto texture = std::make_unique<GLTexture>(*upload, m_maxAnisotropy);
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
