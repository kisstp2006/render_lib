#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/backend/IRenderBackend.h"
#include "engine/backend/gl/GLEnvironment.h"
#include "engine/backend/gl/GLMesh.h"
#include "engine/backend/gl/GLShader.h"
#include "engine/backend/gl/GLTexture.h"
#include "engine/render/CascadedShadows.h"
#include "engine/render/GpuTiming.h"
#include "engine/scene/RenderSettings.h"

namespace engine {

// Frame flow:
//   shadow pass -> MSAA HDR pass (PBR + skybox) -> resolve -> bloom
//   downsample/upsample chain -> post (exposure/tonemap/sRGB) to backbuffer.
class GLRenderBackend final : public IRenderBackend
{
public:
    void Init(Window& window, const RenderBackendConfig& config) override;
    void Shutdown() override;
    void Resize(int width, int height) override;
    void RenderFrame(const RenderFrameData& frame) override;
    void RequestScreenshot(const std::string& path) override { m_screenshotPath = path; }
    BackendFrameStats GetFrameStats() const override { return m_frameStats; }
    BackendCapabilities GetCapabilities() const override { return m_capabilities; }
    bool SetPresentMode(PresentMode mode) override;
    const char* Name() const override { return "OpenGL 4.6"; }

private:
    void InitShadowMap();
    void CreateSceneTargets(int width, int height);
    void DestroySceneTargets();
    void RenderBloom(unsigned int sourceTexture, float threshold, float exposure);
    unsigned int ResolveTemporalAA(const Scene& scene, const Camera& camera,
                                   const glm::mat4& jitteredViewProjection);
    unsigned int GetOrCreateColorLut(const std::shared_ptr<ColorGradingLutData>& data);
    void SaveScreenshot();
    void InitLocalLightResources();
    void DestroyLocalLightResources();
    void RenderLocalLightShadows(const RenderFrameData& frame);
    void BindLocalLights();
    void UpdateLightCookieAtlas();
    void RenderDebugOverlay(const RenderFrameData& frame);
    void RefreshGpuFrameTotal();

    GLMesh& GetOrCreateMesh(const std::shared_ptr<MeshData>& data);
    GLTexture& GetOrCreateTexture(const std::shared_ptr<TextureData>& data);
    void BindMaterialTexture(const std::shared_ptr<TextureData>& map, int unit, GLTexture& fallback);

    int m_width = 0, m_height = 0;
    int m_msaaSamples = 4;
    float m_maxAnisotropy = 8.0f;
    bool m_gpuTimingEnabled = true;
    Window* m_window = nullptr;
    PresentMode m_presentMode = PresentMode::VSync;
    BackendCapabilities m_capabilities;

    std::unique_ptr<GLShader> m_pbrShader;
    std::unique_ptr<GLShader> m_shadowShader;
    std::unique_ptr<GLShader> m_pointShadowShader;
    std::unique_ptr<GLShader> m_skyShader;
    std::unique_ptr<GLShader> m_bloomDownShader;
    std::unique_ptr<GLShader> m_bloomUpShader;
    std::unique_ptr<GLShader> m_taaShader;
    std::unique_ptr<GLShader> m_fxaaShader;
    std::unique_ptr<GLShader> m_postShader;
    std::unique_ptr<GLShader> m_debugOverlayShader;

    std::unique_ptr<GLEnvironment> m_environment;

    // Four independently sized directional-light cascades.
    unsigned int m_shadowFbo = 0;
    std::array<unsigned int, kShadowCascadeCount> m_shadowMaps{};
    std::array<int, kShadowCascadeCount> m_shadowSizes{2048, 2048, 1024, 1024};
    unsigned int m_shadowTimeQueries[2]{};
    int m_shadowQueryIndex = 0;
    bool m_shadowQueryIssued = false;
    GpuTimingAccumulator m_shadowTiming;

    static constexpr int kMaxPointLights = 8;
    static constexpr int kMaxSpotLights = 4;
    static constexpr int kMaxAreaLights = 4;
    static constexpr int kMaxPointShadows = 4;
    static constexpr int kLocalShadowTileSize = 1024;
    static constexpr int kLocalShadowAtlasSize = 4096;
    static constexpr int kCookieAtlasSize = 1024;
    static constexpr int kCookieTileSize = 256;

    struct LocalLightFrameData
    {
        int PointCount = 0;
        int SpotCount = 0;
        int AreaCount = 0;
        std::array<const PointLight*, kMaxPointLights> Points{};
        std::array<const SpotLight*, kMaxSpotLights> Spots{};
        std::array<const AreaLight*, kMaxAreaLights> Areas{};
        std::array<int, kMaxPointLights> PointShadowSlots{};
        std::array<int, kMaxPointLights> PointCookieSlots{};
        std::array<glm::vec3, kMaxSpotLights> SpotDirections{};
        std::array<glm::mat4, kMaxSpotLights> SpotMatrices{};
        std::array<glm::vec4, kMaxSpotLights> SpotShadowRects{};
        std::array<int, kMaxSpotLights> SpotCookieSlots{};
        std::array<glm::vec3, kMaxAreaLights> AreaDirections{};
        std::array<glm::mat4, kMaxAreaLights> AreaMatrices{};
        std::array<glm::vec3, kMaxAreaLights> AreaRights{};
        std::array<glm::vec3, kMaxAreaLights> AreaUps{};
        std::array<glm::vec4, kMaxAreaLights> AreaShadowRects{};
        std::array<int, kMaxAreaLights> AreaCookieSlots{};
    } m_localLights;

    unsigned int m_localShadowAtlasFbo = 0;
    unsigned int m_localShadowAtlas = 0;
    unsigned int m_pointShadowFbo = 0;
    unsigned int m_pointShadowArray = 0;
    int m_pointShadowSize = 512;
    unsigned int m_cookieAtlas = 0;
    std::unordered_map<const TextureData*, int> m_cookieSlots;
    unsigned int m_localShadowTimeQueries[2]{};
    int m_localShadowQueryIndex = 0;
    bool m_localShadowQueryIssued = false;
    GpuTimingAccumulator m_localShadowTiming;

    unsigned int m_postTimeQueries[2]{};
    int m_postQueryIndex = 0;
    bool m_postQueryIssued = false;
    GpuTimingAccumulator m_postTiming;
    unsigned int m_mainTimeQueries[2]{};
    int m_mainQueryIndex = 0;
    bool m_mainQueryIssued = false;
    BackendFrameStats m_frameStats;
    float m_directionalShadowMilliseconds = 0.0f;
    float m_localShadowMilliseconds = 0.0f;

    // Auto-exposure state
    float m_autoExposure = 1.0f;
    double m_lastFrameTime = 0.0;
    unsigned int m_exposurePbos[2]{};
    int m_exposurePboIndex = 0;
    int m_exposurePboFrames = 0;

    // MSAA HDR scene target + resolve texture
    unsigned int m_msaaFbo = 0;
    unsigned int m_msaaColorRbo = 0;
    unsigned int m_msaaVelocityRbo = 0;
    unsigned int m_msaaDepthRbo = 0;
    unsigned int m_resolveFbo = 0;
    unsigned int m_hdrColorTex = 0;
    unsigned int m_velocityTex = 0;
    unsigned int m_depthTex = 0;

    std::array<unsigned int, 2> m_taaFbos{};
    std::array<unsigned int, 2> m_taaHistoryColor{};
    std::array<unsigned int, 2> m_taaHistoryDepth{};
    int m_taaHistoryIndex = 0;
    bool m_taaHistoryValid = false;
    uint64_t m_taaFrameIndex = 0;
    AntiAliasingMode m_previousAaMode = AntiAliasingMode::None;
    glm::mat4 m_previousViewProjection{1.0f};
    glm::vec3 m_previousCameraPosition{0.0f};
    glm::vec3 m_previousCameraForward{0.0f, 0.0f, -1.0f};
    float m_previousCameraFov = 60.0f;
    const Scene* m_previousScene = nullptr;
    std::unordered_map<uint64_t, glm::mat4> m_previousTransforms;

    unsigned int m_postFbo = 0;
    unsigned int m_postColorTex = 0;

    struct BloomLevel
    {
        unsigned int Texture = 0;
        unsigned int Fbo = 0;
        int Width = 0, Height = 0;
    };
    std::vector<BloomLevel> m_bloomChain;

    unsigned int m_emptyVao = 0;
    unsigned int m_debugOverlayTexture = 0;

    std::unique_ptr<GLTexture> m_defaultWhite;
    std::unique_ptr<GLTexture> m_defaultNormal;

    std::string m_screenshotPath;

    // Keep the CPU assets alive for as long as their GPU counterparts are
    // cached. Raw-pointer keys could otherwise alias a newly allocated asset
    // after the original shared_ptr was released.
    std::unordered_map<std::shared_ptr<MeshData>, std::unique_ptr<GLMesh>> m_meshCache;
    std::unordered_map<std::shared_ptr<TextureData>, std::unique_ptr<GLTexture>> m_textureCache;
    std::unordered_map<std::shared_ptr<ColorGradingLutData>, unsigned int> m_colorLutCache;
};

} // namespace engine
