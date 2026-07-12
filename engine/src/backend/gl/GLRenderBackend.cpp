#include "engine/backend/gl/GLRenderBackend.h"
#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/core/Window.h"
#include "engine/scene/Scene.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <stdexcept>
#include <string>

namespace engine {

static void APIENTRY GLDebugCallback(GLenum /*source*/, GLenum type, unsigned int /*id*/, GLenum severity,
                                      GLsizei /*length*/, const char* message, const void* /*userParam*/)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return;

    const bool isError = (type == GL_DEBUG_TYPE_ERROR);
    (isError ? log::Error : log::Warn)(std::string("[GL] ") + message);
}

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

    InitShadowMap();

    log::Info("GL renderer initialized");
}

void GLRenderBackend::InitShadowMap()
{
    glGenFramebuffers(1, &m_shadowFbo);
    glGenTextures(1, &m_shadowMap);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, m_shadowSize, m_shadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    // Manual PCF is done in pbr.frag against a plain sampler2D, so this must
    // stay a regular (non-shadow) sampler - GL_TEXTURE_COMPARE_MODE would
    // otherwise force sampler2DShadow semantics and produce undefined results.
    const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Shadow map framebuffer incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLRenderBackend::Shutdown()
{
    m_meshCache.clear();
    m_pbrShader.reset();
    m_shadowShader.reset();

    if (m_shadowMap) glDeleteTextures(1, &m_shadowMap);
    if (m_shadowFbo) glDeleteFramebuffers(1, &m_shadowFbo);
}

void GLRenderBackend::Resize(int width, int height)
{
    m_width = width;
    m_height = height;
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

void GLRenderBackend::RenderFrame(const Scene& scene, const Camera& camera)
{
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

    // --- Main pass ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
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
    m_pbrShader->SetVec3("uAmbientColor", scene.AmbientColor);

    const auto& lights = scene.PointLights();
    const int lightCount = static_cast<int>(lights.size() < 8 ? lights.size() : 8);
    m_pbrShader->SetInt("uPointLightCount", lightCount);
    for (int i = 0; i < lightCount; ++i)
    {
        const std::string base = "uPointLights[" + std::to_string(i) + "].";
        m_pbrShader->SetVec3(base + "Position", lights[i].Position);
        m_pbrShader->SetVec3(base + "Color", lights[i].Color * lights[i].Intensity);
        m_pbrShader->SetFloat(base + "Radius", lights[i].Radius);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    m_pbrShader->SetInt("uShadowMap", 0);

    for (const auto& instance : scene.Instances())
    {
        m_pbrShader->SetMat4("uModel", instance.Transform);
        m_pbrShader->SetMat4("uNormalMatrix", glm::transpose(glm::inverse(instance.Transform)));

        m_pbrShader->SetVec3("uAlbedo", instance.Mat.Albedo);
        m_pbrShader->SetFloat("uMetallic", instance.Mat.Metallic);
        m_pbrShader->SetFloat("uRoughness", instance.Mat.Roughness);
        m_pbrShader->SetVec3("uEmissive", instance.Mat.Emissive);
        m_pbrShader->SetFloat("uAO", instance.Mat.AmbientOcclusion);
        m_pbrShader->SetFloat("uSpecularF0", instance.Mat.SpecularF0);

        GetOrCreateMesh(*instance.Mesh).Draw();
    }
}

} // namespace engine
