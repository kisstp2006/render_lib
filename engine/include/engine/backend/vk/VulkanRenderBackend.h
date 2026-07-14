#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/backend/IRenderBackend.h"
#include "engine/backend/vk/VulkanResources.h"
#include "engine/backend/vk/VulkanPipelineCache.h"
#include "engine/asset/ColorGrading.h"
#include "engine/scene/Material.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Environment.h"
#include "engine/scene/RenderSettings.h"
#include "engine/scene/Texture.h"
#include "engine/render/GpuTiming.h"
#include "engine/render/AsyncRenderResources.h"
#include "engine/profiling/GpuProfiler.h"

namespace engine {

class Scene;

// Vulkan 1.3 renderer that consumes the same CPU-side Scene/Camera contract as
// OpenGL. Kept as a separate concrete class (not
// forced through a generic RHI) since Vulkan's resource/sync model is too
// different from GL's to share productively. Scene/frame preparation,
// temporal/cascade/exposure/timing algorithms and shader math are shared;
// native allocation, synchronization and command recording stay here.
class VulkanRenderBackend final : public IRenderBackend
{
public:
    void Init(Window& window, const RenderBackendConfig& config) override;
    void Shutdown() override;
    void Resize(int width, int height) override;
    void RenderFrame(const RenderFrameData& frame) override;
    void RequestScreenshot(const std::string& path) override { m_screenshotPath = path; }
    void RequestHdrScreenshot(const std::string& path) override { m_hdrScreenshotPath = path; }
    BackendFrameStats GetFrameStats() const override { return m_frameStats; }
    BackendCapabilities GetCapabilities() const override { return m_capabilities; }
    BackendResourceStats GetResourceStats() const override;
    PipelineCacheStatistics GetPipelineCacheStats() const override { return m_pipelineCacheStats; }
    rendergraph::Statistics GetRenderGraphStats() const override { return m_renderGraph.Stats(); }
    debug::FrameDebugSnapshot GetFrameDebugSnapshot() const override;
    bool CaptureFrameDebugResource(uint64_t resourceId, uint32_t mipLevel,
                                   uint32_t layer,
                                   debug::FrameDebugPreview& preview) override;
    bool SetPresentMode(PresentMode mode) override;
    const char* Name() const override { return "Vulkan 1.3 PBR"; }

private:
    static constexpr int kFramesInFlight = 2;

    struct QueueFamilyIndices
    {
        std::optional<uint32_t> Graphics;
        std::optional<uint32_t> Present;
        std::optional<uint32_t> Transfer;
        bool IsComplete() const { return Graphics.has_value() && Present.has_value() && Transfer.has_value(); }
    };

    struct GpuMesh
    {
        vulkan::Buffer VertexBuffer;
        vulkan::Buffer IndexBuffer;
        uint32_t IndexCount = 0;
    };

    struct GpuTexture
    {
        vulkan::Image Image;
        VkSampler Sampler = VK_NULL_HANDLE;
    };

    struct GpuMaterial
    {
        std::vector<vulkan::Buffer> UniformBuffers;
        std::vector<VkDescriptorSet> DescriptorSets;
    };

    struct GpuPanorama
    {
        vulkan::Image Image;
        VkSampler Sampler = VK_NULL_HANDLE;
    };

    struct BloomChain
    {
        std::array<vulkan::Image, 6> Levels{};
        std::array<VkDescriptorSet, 6> DownsampleSets{};
        std::array<VkDescriptorSet, 5> UpsampleSets{};
    };

    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface(Window& window);
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapchain(int width, int height);
    void CreateImageViews();
    void CreateDepthResources();
    void CreateShadowResources();
    void DestroyShadowResources();
    void CreateShaderInfrastructure();
    void DestroyShaderInfrastructure();
    VkShaderModule LoadShader(const std::filesystem::path& relativePath,
                              const std::vector<ShaderDefine>& defines = {},
                              PipelineCacheStatistics* statistics = nullptr);
    std::vector<VkShaderModule> LoadShadersParallel(
        const std::vector<std::filesystem::path>& relativePaths);
    void CreateGraphicsPipeline();
    void DestroyGraphicsPipeline();
    void CreatePostInfrastructure();
    void DestroyPostInfrastructure();
    void CreatePostTargets();
    void DestroyPostTargets();
    void CreatePostPipelines();
    void DestroyPostPipelines();
    void PreparePost(const RenderFrameData& frame, uint32_t imageIndex);
    void UpdateAutoExposure(const RenderFrameData& frame);
    void RecordAutoExposure(VkCommandBuffer commandBuffer,
                            const RenderFrameData& frame, uint32_t imageIndex);
    void RecordPost(VkCommandBuffer commandBuffer, const RenderFrameData& frame,
                    uint32_t imageIndex);
    void PrepareDebugOverlay(const RenderFrameData& frame);
    void RecordDebugOverlay(VkCommandBuffer commandBuffer,
                            const RenderFrameData& frame, uint32_t imageIndex);
    const vulkan::Image& RecordTemporalAA(VkCommandBuffer commandBuffer,
                                          const RenderFrameData& frame,
                                          uint32_t imageIndex);
    const vulkan::Image& GetOrCreateColorLut(
        const std::shared_ptr<ColorGradingLutData>& data);
    void CreateDefaultResources();
    void DestroySceneResources();
    void CreateEnvironmentInfrastructure();
    void DestroyEnvironmentInfrastructure();
    void CreateEnvironmentResources();
    void DestroyEnvironmentResources();
    void EnsureEnvironmentBaked(const RenderFrameData& frame);
    const GpuPanorama& GetOrCreatePanorama(const std::shared_ptr<HdrImageData>& image);
    void UpdateEnvironmentDescriptors();
    void CreateLocalLightResources();
    void DestroyLocalLightResources();
    void PrepareLocalLights(const RenderFrameData& frame);
    void RecordLocalLightShadows(VkCommandBuffer commandBuffer, const Scene& scene);
    void UpdateFrameUniforms(const RenderFrameData& frame);
    VkCommandBuffer BeginImmediateCommands();
    void EndImmediateCommands(VkCommandBuffer commandBuffer);
    const GpuMesh& GetOrCreateMesh(const std::shared_ptr<MeshData>& mesh);
    const GpuTexture& GetOrCreateTexture(const std::shared_ptr<TextureData>& texture,
                                         const std::shared_ptr<TextureData>& fallback);
    GpuMaterial& GetOrCreateMaterial(const Material& material);
    void UpdateMaterial(const Material& material, GpuMaterial& gpuMaterial, uint32_t frameIndex);
    void SaveScreenshot(const vulkan::Buffer& readbackBuffer, const std::string& path) const;
    void SaveHdrScreenshot(const vulkan::Buffer& readbackBuffer, const std::string& path) const;
    void CreateSyncObjects();
    void CreatePerformanceQueries();
    void DestroyPerformanceQueries();
    void ReadPerformanceQueries(uint32_t frameIndex);
    profiling::GpuMemoryStatistics QueryGpuMemory() const;
    void CreateCommandObjects();
    void CreateAsyncResourceInfrastructure();
    void DestroyAsyncResourceInfrastructure();
    void ReclaimTransferUploads();
    uint64_t QueueBufferUploads(const std::vector<std::pair<const void*, std::pair<VkDeviceSize, VkBuffer>>>& uploads);
    uint64_t QueueTextureUpload(const void* pixels, VkDeviceSize byteCount,
                                VkImage image, uint32_t mipLevels,
                                const std::vector<VkBufferImageCopy>& copyRegions);
    void RecreateSwapchain(int width, int height);
    void DestroySwapchain();
    void LoadDebugUtils();
    void SetDebugName(VkObjectType type, uint64_t handle, std::string_view name) const;
    void BeginDebugLabel(VkCommandBuffer commandBuffer, std::string_view name,
                         const std::array<float, 4>& color) const;
    void EndDebugLabel(VkCommandBuffer commandBuffer) const;

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    bool IsDeviceSuitable(VkPhysicalDevice device) const;

    Window* m_window = nullptr;
    RenderBackendConfig m_config;
    BackendCapabilities m_capabilities;
    PresentMode m_presentMode = PresentMode::VSync;
    bool m_presentModeExact = true;
    bool m_validationEnabled = false;
    bool m_debugUtilsEnabled = false;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT m_setDebugObjectName = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT m_beginDebugLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT m_endDebugLabel = nullptr;
    bool m_samplerAnisotropySupported = false;
    bool m_pipelineStatisticsSupported = false;
    bool m_memoryBudgetSupported = false;
    bool m_pipelineCreationFeedbackSupported = false;
    float m_maxSamplerAnisotropy = 1.0f;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent{};
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<vulkan::Image> m_depthImages;
    std::array<vulkan::Image, kFramesInFlight> m_msaaDepthImages{};
    VkSampleCountFlagBits m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    vulkan::ResourceAllocator m_resources;
    static constexpr uint64_t kStagingRingBytes = 64ull * 1024ull * 1024ull;
    static constexpr uint64_t kFrameArenaBytes = 4ull * 1024ull * 1024ull;
    VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;
    VkSemaphore m_transferTimeline = VK_NULL_HANDLE;
    uint64_t m_transferTimelineValue = 0;
    uint64_t m_pendingTransferWaitValue = 0;
    vulkan::Buffer m_stagingRingBuffer;
    void* m_stagingRingMapped = nullptr;
    vulkan::Buffer m_frameArenaBuffer;
    void* m_frameArenaMapped = nullptr;
    std::unique_ptr<render::StagingRingAllocator> m_stagingRing;
    std::unique_ptr<render::FrameGpuArena> m_frameGpuArena;
    render::DeferredReleaseQueue m_deferredRelease;
    struct PendingTransferCommand
    {
        VkCommandBuffer Command = VK_NULL_HANDLE;
        uint64_t TimelineValue = 0;
    };
    std::vector<PendingTransferCommand> m_pendingTransferCommands;
    vulkan::PipelineCacheStore m_pipelineCache;
    PipelineCacheStatistics m_pipelineCacheStats;
    rendergraph::Config m_renderGraphConfig;
    rendergraph::RenderGraph m_renderGraph;
    std::filesystem::path m_shaderCacheDirectory;
    std::unique_ptr<concurrency::TaskSystem> m_pipelineTasks;
    std::mutex m_shaderLoadMutex;
    VkShaderModule m_pbrVertexShader = VK_NULL_HANDLE;
    VkShaderModule m_pbrFragmentShader = VK_NULL_HANDLE;
    VkShaderModule m_shadowVertexShader = VK_NULL_HANDLE;
    VkShaderModule m_shadowFragmentShader = VK_NULL_HANDLE;
    VkShaderModule m_skyVertexShader = VK_NULL_HANDLE;
    VkShaderModule m_skyFragmentShader = VK_NULL_HANDLE;
    VkShaderModule m_boundsDebugVertexShader = VK_NULL_HANDLE;
    VkShaderModule m_boundsDebugFragmentShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_frameDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_materialDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_shadowDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout m_pbrPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_boundsDebugPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pbrPipeline = VK_NULL_HANDLE;
    VkPipeline m_skyPipeline = VK_NULL_HANDLE;
    VkPipeline m_shadowPipeline = VK_NULL_HANDLE;
    VkPipeline m_boundsDebugPipeline = VK_NULL_HANDLE;

    VkShaderModule m_fullscreenVertexShader = VK_NULL_HANDLE;
    VkShaderModule m_postFragmentShader = VK_NULL_HANDLE;
    VkShaderModule m_fxaaFragmentShader = VK_NULL_HANDLE;
    VkShaderModule m_bloomDownsampleShader = VK_NULL_HANDLE;
    VkShaderModule m_bloomUpsampleShader = VK_NULL_HANDLE;
    VkShaderModule m_taaFragmentShader = VK_NULL_HANDLE;
    VkShaderModule m_exposureShader = VK_NULL_HANDLE;
    VkShaderModule m_debugOverlayFragmentShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_postDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_fxaaDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_bloomDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_taaDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_exposureDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_debugOverlayDescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_postPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_fxaaPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_bloomPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_taaPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_exposurePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_debugOverlayPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_postSwapchainPipeline = VK_NULL_HANDLE;
    VkPipeline m_postLdrPipeline = VK_NULL_HANDLE;
    VkPipeline m_fxaaPipeline = VK_NULL_HANDLE;
    VkPipeline m_bloomDownsamplePipeline = VK_NULL_HANDLE;
    VkPipeline m_bloomUpsamplePipeline = VK_NULL_HANDLE;
    VkPipeline m_taaPipeline = VK_NULL_HANDLE;
    VkPipeline m_exposurePipeline = VK_NULL_HANDLE;
    VkPipeline m_debugOverlayPipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_postDescriptorPool = VK_NULL_HANDLE;
    VkSampler m_postSampler = VK_NULL_HANDLE;
    std::vector<vulkan::Image> m_hdrImages;
    std::vector<vulkan::Image> m_ldrImages;
    std::vector<vulkan::Image> m_velocityImages;
    std::array<vulkan::Image, kFramesInFlight> m_msaaHdrImages{};
    std::array<vulkan::Image, kFramesInFlight> m_msaaVelocityImages{};
    std::vector<BloomChain> m_bloomChains;
    std::array<vulkan::Image, 2> m_taaHistoryColor{};
    std::array<vulkan::Image, 2> m_taaHistoryDepth{};
    std::vector<vulkan::Buffer> m_postUniformBuffers;
    std::vector<VkDescriptorSet> m_postDescriptorSets;
    std::vector<VkDescriptorSet> m_fxaaDescriptorSets;
    std::vector<VkDescriptorSet> m_taaDescriptorSets;
    std::vector<VkDescriptorSet> m_exposureDescriptorSets;
    std::array<vulkan::Buffer, kFramesInFlight> m_exposureBuffers{};
    std::array<bool, kFramesInFlight> m_exposureReadbackValid{};
    std::array<vulkan::Image, kFramesInFlight> m_debugOverlayImages{};
    std::array<vulkan::Buffer, kFramesInFlight> m_debugOverlayStaging{};
    std::array<VkDescriptorSet, kFramesInFlight> m_debugOverlayDescriptorSets{};
    std::array<bool, kFramesInFlight> m_debugOverlayImageInitialized{};
    std::unordered_map<std::shared_ptr<ColorGradingLutData>, vulkan::Image> m_colorLutCache;
    std::shared_ptr<ColorGradingLutData> m_defaultColorLut;
    std::shared_ptr<ColorGradingLutData> m_activeColorLut;
    float m_autoExposure = 1.0f;
    double m_lastExposureTime = 0.0;
    bool m_taaActive = false;
    bool m_taaHistoryValid = false;
    int m_taaHistoryIndex = 0;
    uint64_t m_taaFrameIndex = 0;
    glm::mat4 m_currentViewProjection{1.0f};
    glm::mat4 m_previousViewProjection{1.0f};
    glm::vec3 m_previousCameraPosition{0.0f};
    glm::vec3 m_previousCameraForward{0.0f, 0.0f, -1.0f};
    float m_previousCameraFov = 60.0f;
    const Scene* m_previousScene = nullptr;
    AntiAliasingMode m_previousAaMode = AntiAliasingMode::None;
    std::unordered_map<uint64_t, glm::mat4> m_previousTransforms;
    std::vector<vulkan::Buffer> m_frameUniformBuffers;
    std::vector<VkDescriptorSet> m_frameDescriptorSets;
    std::array<std::array<vulkan::Image, 4>, kFramesInFlight> m_shadowMaps{};
    std::array<std::array<vulkan::Buffer, 4>, kFramesInFlight> m_shadowUniformBuffers{};
    std::array<std::array<VkDescriptorSet, 4>, kFramesInFlight> m_shadowDescriptorSets{};
    std::array<uint32_t, 4> m_shadowSizes{2048, 2048, 1024, 1024};
    VkSampler m_shadowSampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_environmentBakeDescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_environmentBakePipelineLayout = VK_NULL_HANDLE;
    VkShaderModule m_environmentSourceShader = VK_NULL_HANDLE;
    VkShaderModule m_irradianceShader = VK_NULL_HANDLE;
    VkShaderModule m_prefilterShader = VK_NULL_HANDLE;
    VkShaderModule m_brdfShader = VK_NULL_HANDLE;
    VkPipeline m_environmentSourcePipeline = VK_NULL_HANDLE;
    VkPipeline m_irradiancePipeline = VK_NULL_HANDLE;
    VkPipeline m_prefilterPipeline = VK_NULL_HANDLE;
    VkPipeline m_brdfPipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_environmentSourceDescriptor = VK_NULL_HANDLE;
    VkDescriptorSet m_irradianceDescriptor = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 8> m_prefilterDescriptors{};
    VkDescriptorSet m_brdfDescriptor = VK_NULL_HANDLE;
    vulkan::Image m_environmentCube;
    vulkan::Image m_irradianceCube;
    vulkan::Image m_prefilterCube;
    vulkan::Image m_brdfLut;
    VkImageView m_environmentStorageView = VK_NULL_HANDLE;
    VkImageView m_irradianceStorageView = VK_NULL_HANDLE;
    std::array<VkImageView, 8> m_prefilterStorageViews{};
    VkSampler m_environmentSampler = VK_NULL_HANDLE;
    VkSampler m_brdfSampler = VK_NULL_HANDLE;
    std::unordered_map<const HdrImageData*, GpuPanorama> m_panoramaCache;
    uint64_t m_environmentStaticSignature = 0;
    uint64_t m_environmentDirectionSignature = 0;
    double m_lastEnvironmentBakeTime = -1000.0;
    bool m_environmentImagesInitialized = false;

    VkShaderModule m_pointShadowVertexShader = VK_NULL_HANDLE;
    VkShaderModule m_pointShadowFragmentShader = VK_NULL_HANDLE;
    VkPipeline m_pointShadowPipeline = VK_NULL_HANDLE;
    vulkan::Image m_pointShadowArray;
    VkImageView m_pointShadowCubeArrayView = VK_NULL_HANDLE;
    std::array<VkImageView, 24> m_pointShadowFaceViews{};
    std::array<vulkan::Buffer, 24> m_pointShadowUniformBuffers{};
    std::array<VkDescriptorSet, 24> m_pointShadowDescriptorSets{};
    vulkan::Image m_localShadowAtlas;
    std::array<vulkan::Buffer, 8> m_projectedShadowUniformBuffers{};
    std::array<VkDescriptorSet, 8> m_projectedShadowDescriptorSets{};
    vulkan::Image m_cookieAtlas;
    VkSampler m_pointShadowSampler = VK_NULL_HANDLE;
    VkSampler m_localShadowSampler = VK_NULL_HANDLE;
    VkSampler m_cookieSampler = VK_NULL_HANDLE;
    std::unordered_map<const TextureData*, int> m_cookieSlots;
    std::array<int, 8> m_pointShadowSlots{};
    std::array<int, 8> m_pointCookieSlots{};
    std::array<glm::vec4, 4> m_spotShadowRects{};
    std::array<int, 4> m_spotCookieSlots{};
    std::array<glm::vec4, 4> m_areaShadowRects{};
    std::array<int, 4> m_areaCookieSlots{};
    std::array<glm::mat4, 4> m_spotShadowMatrices{};
    std::array<glm::mat4, 4> m_areaShadowMatrices{};
    int m_pointShadowCount = 0;
    int m_projectedShadowCount = 0;
    bool m_localShadowImagesInitialized = false;

    std::unordered_map<const MeshData*, GpuMesh> m_meshCache;
    std::unordered_map<const TextureData*, GpuTexture> m_textureCache;
    std::unordered_map<const Material*, GpuMaterial> m_materialCache;
    std::shared_ptr<TextureData> m_defaultWhiteData;
    std::shared_ptr<TextureData> m_defaultNormalData;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlightFences;
    enum GpuProfilerPass : uint32_t
    {
        DirectionalShadowPass,
        LocalShadowPass,
        MainHdrPass,
        PostProcessPass,
        DebugUiPass,
        GpuProfilerPassCount
    };
    static constexpr uint32_t kTimestampCountPerFrame = GpuProfilerPassCount * 2;
    static constexpr uint32_t kPipelineCounterCount = 5;
    VkQueryPool m_timestampQueryPool = VK_NULL_HANDLE;
    VkQueryPool m_pipelineStatisticsQueryPool = VK_NULL_HANDLE;
    float m_timestampPeriodNanoseconds = 1.0f;
    bool m_gpuTimingSupported = false;
    std::array<bool, kFramesInFlight> m_timestampFrameWritten{};
    std::array<bool, kFramesInFlight> m_pipelineStatisticsFrameWritten{};
    std::array<uint64_t, kFramesInFlight> m_gpuProfileFrameIds{};
    std::array<uint64_t, kFramesInFlight> m_gpuProfileDrawCalls{};
    std::array<uint64_t, kFramesInFlight> m_gpuProfileDispatches{};
    std::array<bool, kFramesInFlight> m_timestampLogShadows{};
    std::array<bool, kFramesInFlight> m_timestampLogPost{};
    std::array<AntiAliasingMode, kFramesInFlight> m_timestampAaMode{};
    GpuTimingAccumulator m_shadowTiming;
    GpuTimingAccumulator m_localShadowTiming;
    GpuTimingAccumulator m_mainTiming;
    GpuTimingAccumulator m_postTiming;
    BackendFrameStats m_frameStats;
    uint64_t m_gpuProfileFrameIndex = 0;
    uint64_t m_gpuDrawCallsThisFrame = 0;
    uint64_t m_gpuDispatchesThisFrame = 0;
    uint32_t m_currentFrame = 0;
    uint32_t m_lastFrameDebugFrameSlot = 0;
    uint32_t m_lastFrameDebugImageIndex = 0;
    bool m_lastFrameDebugTaaActive = false;
    bool m_lastFrameDebugFxaaActive = false;
    bool m_lastFrameDebugPostEnabled = false;
    bool m_hasFrameDebugFrame = false;
    // Diagnostic readbacks stay allocated until shutdown. Some drivers can
    // recycle freshly freed host-visible allocations into the next frame's
    // overlay upload before presentation has fully retired; retaining this
    // bounded diagnostic cache also makes repeated mip/layer inspection cheaper.
    std::vector<vulkan::Buffer> m_frameDebugReadbackBuffers;
    std::string m_screenshotPath;
    std::string m_hdrScreenshotPath;
};

} // namespace engine
