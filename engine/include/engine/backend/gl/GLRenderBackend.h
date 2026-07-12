#pragma once

#include <memory>
#include <unordered_map>

#include "engine/backend/IRenderBackend.h"
#include "engine/backend/gl/GLMesh.h"
#include "engine/backend/gl/GLShader.h"

namespace engine {

class GLRenderBackend final : public IRenderBackend
{
public:
    void Init(Window& window) override;
    void Shutdown() override;
    void Resize(int width, int height) override;
    void RenderFrame(const Scene& scene, const Camera& camera) override;
    const char* Name() const override { return "OpenGL 4.6"; }

private:
    void InitShadowMap();
    GLMesh& GetOrCreateMesh(const MeshData& data);

    Window* m_window = nullptr;
    int m_width = 0, m_height = 0;

    std::unique_ptr<GLShader> m_pbrShader;
    std::unique_ptr<GLShader> m_shadowShader;

    unsigned int m_shadowFbo = 0;
    unsigned int m_shadowMap = 0;
    int m_shadowSize = 2048;

    std::unordered_map<const MeshData*, std::unique_ptr<GLMesh>> m_meshCache;
};

} // namespace engine
