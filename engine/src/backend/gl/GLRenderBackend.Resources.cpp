#include "engine/backend/gl/GLRenderBackend.h"

#include "engine/core/Log.h"
#include "engine/core/Window.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

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
    const auto white = textures::MakeSolidColor({1.0f, 1.0f, 1.0f, 1.0f}, false);
    const auto flatNormal = textures::MakeFlatNormal();
    m_defaultWhite = std::make_unique<GLTexture>(*white);
    m_defaultNormal = std::make_unique<GLTexture>(*flatNormal);
    InitShadowMap();
    CreateSceneTargets(m_width, m_height);

    glGenBuffers(2, m_exposurePbos);
    for (unsigned int pbo : m_exposurePbos)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, sizeof(float) * 4, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glGenQueries(2, m_shadowTimeQueries);
    log::Info("GL renderer initialized (HDR + MSAA + IBL + bloom)");
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
    glDeleteTextures(kShadowCascadeCount, m_shadowMaps.data());
    if (m_shadowFbo) glDeleteFramebuffers(1, &m_shadowFbo);
    if (m_spotShadowMap) glDeleteTextures(1, &m_spotShadowMap);
    if (m_spotShadowFbo) glDeleteFramebuffers(1, &m_spotShadowFbo);
    glDeleteBuffers(2, m_exposurePbos);
    glDeleteQueries(2, m_shadowTimeQueries);
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
    auto texture = std::make_unique<GLTexture>(*data);
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
