#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/backend/IRenderBackend.h"
#include "engine/backend/gl/GLEnvironment.h"
#include "engine/backend/gl/GLMesh.h"
#include "engine/backend/gl/GLShader.h"
#include "engine/backend/gl/GLTexture.h"

namespace engine {

// Frame flow:
//   shadow pass -> MSAA HDR pass (PBR + skybox) -> resolve -> bloom
//   downsample/upsample chain -> post (exposure/tonemap/sRGB) to backbuffer.
class GLRenderBackend final : public IRenderBackend
{
public:
    void Init(Window& window) override;
    void Shutdown() override;
    void Resize(int width, int height) override;
    void RenderFrame(const Scene& scene, const Camera& camera) override;
    void RequestScreenshot(const std::string& path) override { m_screenshotPath = path; }
    const char* Name() const override { return "OpenGL 4.6"; }

private:
    void InitShadowMap();
    void CreateSceneTargets(int width, int height);
    void DestroySceneTargets();
    void RenderBloom(float threshold, float exposure);
    void SaveScreenshot();

    GLMesh& GetOrCreateMesh(const std::shared_ptr<MeshData>& data);
    GLTexture& GetOrCreateTexture(const std::shared_ptr<TextureData>& data);
    void BindMaterialTexture(const std::shared_ptr<TextureData>& map, int unit, GLTexture& fallback);

    Window* m_window = nullptr;
    int m_width = 0, m_height = 0;
    int m_msaaSamples = 4;

    std::unique_ptr<GLShader> m_pbrShader;
    std::unique_ptr<GLShader> m_shadowShader;
    std::unique_ptr<GLShader> m_skyShader;
    std::unique_ptr<GLShader> m_bloomDownShader;
    std::unique_ptr<GLShader> m_bloomUpShader;
    std::unique_ptr<GLShader> m_postShader;

    std::unique_ptr<GLEnvironment> m_environment;

    // Sun shadow map
    unsigned int m_shadowFbo = 0;
    unsigned int m_shadowMap = 0;
    int m_shadowSize = 4096;

    // Spot (flashlight) shadow map
    unsigned int m_spotShadowFbo = 0;
    unsigned int m_spotShadowMap = 0;
    int m_spotShadowSize = 2048;

    // Auto-exposure state
    float m_autoExposure = 1.0f;
    double m_lastFrameTime = 0.0;
    unsigned int m_exposurePbos[2]{};
    int m_exposurePboIndex = 0;
    int m_exposurePboFrames = 0;

    // MSAA HDR scene target + resolve texture
    unsigned int m_msaaFbo = 0;
    unsigned int m_msaaColorRbo = 0;
    unsigned int m_msaaDepthRbo = 0;
    unsigned int m_resolveFbo = 0;
    unsigned int m_hdrColorTex = 0;

    struct BloomLevel
    {
        unsigned int Texture = 0;
        unsigned int Fbo = 0;
        int Width = 0, Height = 0;
    };
    std::vector<BloomLevel> m_bloomChain;

    unsigned int m_emptyVao = 0;

    std::unique_ptr<GLTexture> m_defaultWhite;
    std::unique_ptr<GLTexture> m_defaultNormal;

    std::string m_screenshotPath;

    // Keep the CPU assets alive for as long as their GPU counterparts are
    // cached. Raw-pointer keys could otherwise alias a newly allocated asset
    // after the original shared_ptr was released.
    std::unordered_map<std::shared_ptr<MeshData>, std::unique_ptr<GLMesh>> m_meshCache;
    std::unordered_map<std::shared_ptr<TextureData>, std::unique_ptr<GLTexture>> m_textureCache;
};

} // namespace engine
