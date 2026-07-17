#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render/CascadedShadows.h"
#include "engine/render/Batching.h"
#include "engine/backend/IRenderBackend.h"
#include "engine/render/Visibility.h"
#include "engine/scene/Environment.h"
#include "engine/scene/DebugDraw.h"
#include "engine/concurrency/TaskSystem.h"

namespace engine {

class Camera;
class Scene;
struct MeshInstance;
namespace debug { struct DebugOverlayImage; }
struct PointLight;
struct SpotLight;
struct AreaLight;

struct PreparedPointLight
{
    const PointLight* Source = nullptr;
};

struct PreparedSpotLight
{
    const SpotLight* Source = nullptr;
    glm::vec3 Direction{0.0f, -1.0f, 0.0f};
    glm::mat4 Projection{1.0f};
};

struct PreparedAreaLight
{
    const AreaLight* Source = nullptr;
    glm::vec3 Direction{0.0f, -1.0f, 0.0f};
    glm::vec3 Right{1.0f, 0.0f, 0.0f};
    glm::vec3 Up{0.0f, 1.0f, 0.0f};
    glm::mat4 Projection{1.0f};
};

struct PreparedLocalLights
{
    static constexpr uint32_t MaxPointLights = 8;
    static constexpr uint32_t MaxSpotLights = 4;
    static constexpr uint32_t MaxAreaLights = 4;

    std::array<PreparedPointLight, MaxPointLights> Points{};
    std::array<PreparedSpotLight, MaxSpotLights> Spots{};
    std::array<PreparedAreaLight, MaxAreaLights> Areas{};
    uint32_t PointCount = 0;
    uint32_t SpotCount = 0;
    uint32_t AreaCount = 0;
};

struct PreparedRenderCommand
{
    const MeshInstance* Source = nullptr;
    uint32_t InstanceIndex = 0;
    uint32_t IndexCount = 0;
    AxisAlignedBounds WorldBounds;
};

enum class FrameWorkStage : uint8_t
{
    Animation,
    Particles,
    Visibility
};

struct FrameWorkContext
{
    const Scene& SceneData;
    const Camera& CameraData;
    int Width = 1;
    int Height = 1;
    float TimeSeconds = 0.0f;
    float DeltaSeconds = 0.0f;
};

using FrameWorkCallback = std::function<void(const FrameWorkContext&,
                                              const concurrency::CancellationToken&)>;

struct FramePreparationStatistics
{
    uint32_t WorkerCount = 0;
    uint32_t CustomTaskCount = 0;
    uint32_t RenderCommandCount = 0;
    uint32_t ShadowCommandCount = 0;
};

// Backend-neutral, immutable description of one scene view. SceneRenderer
// computes this once per frame; OpenGL and Vulkan consume the same camera,
// sun/day-night state, cascade splits/matrices and selected local lights.
struct RenderFrameData
{
    const Scene* SceneData = nullptr;
    const Camera* CameraData = nullptr;
    int Width = 1;
    int Height = 1;
    float AspectRatio = 1.0f;
    uint64_t FrameIndex = 0;
    float TimeSeconds = 0.0f;
    float DeltaSeconds = 1.0f / 60.0f;

    glm::mat4 View{1.0f};
    glm::mat4 BaseProjection{1.0f};
    glm::vec3 SunDirection{-0.4f, -0.85f, -0.35f};
    glm::vec3 EffectiveSunColor{1.0f};
    DayNightState DayNight{};
    bool ProceduralDayNight = false;
    bool SunShadowsActive = false;
    CascadeShadowData Cascades{};
    PreparedLocalLights LocalLights{};
    std::vector<PreparedRenderCommand> RenderCommands;
    std::vector<PreparedRenderCommand> ShadowCommands;
    std::vector<VisibilityDebugBounds> VisibilityDebug;
    std::vector<DebugLine> DebugLines;
    VisibilityStatistics Visibility{};
    GeometryBatchingStatistics Batching{};
    FramePreparationStatistics Preparation{};
    float TonemapWhitePointScale = 1.0f;
    const debug::DebugOverlayImage* DebugOverlay = nullptr;
};

// Shared scene-rendering front end. It owns no API objects; native buffers,
// textures, command recording and pipelines remain in the selected backend.
class SceneRenderer
{
public:
    explicit SceneRenderer(concurrency::TaskSystem* tasks = nullptr)
        : m_tasks(tasks)
    {
    }

    uint64_t AddFrameWork(FrameWorkStage stage, FrameWorkCallback callback,
                          concurrency::TaskPriority priority = concurrency::TaskPriority::High);
    bool RemoveFrameWork(uint64_t token);
    void ClearFrameWork();
    void ResetFrameHistory()
    {
        m_frame = {};
        m_batcher.Reset();
    }
    void SetTaskSystem(concurrency::TaskSystem* tasks)
    {
        m_tasks = tasks;
    }
    const VisibilityStatistics& GetVisibilityStatistics() const noexcept
    {
        return m_frame.Visibility;
    }
    const GeometryBatchingStatistics& GetBatchingStatistics() const noexcept
    {
        return m_frame.Batching;
    }

    const RenderFrameData& PrepareFrame(const Scene& scene, const Camera& camera,
                                        int width, int height,
                                        const debug::DebugOverlayImage* debugOverlay = nullptr,
                                        float timeSeconds = 0.0f,
                                        float deltaSeconds = 1.0f / 60.0f);

    RenderViewportHandle CreateViewport(IRenderBackend& backend,
                                        const RenderViewportDesc& desc)
    {
        return backend.CreateViewport(desc);
    }
    bool ResizeViewport(IRenderBackend& backend, RenderViewportHandle viewport,
                        uint32_t width, uint32_t height)
    {
        return backend.ResizeViewport(viewport, width, height);
    }
    void DestroyViewport(IRenderBackend& backend, RenderViewportHandle viewport)
    {
        backend.DestroyViewport(viewport);
    }
    bool RenderViewport(IRenderBackend& backend, RenderViewportHandle viewport,
                        const Scene& scene, const Camera& camera,
                        uint32_t width, uint32_t height,
                        float timeSeconds = 0.0f,
                        float deltaSeconds = 1.0f / 60.0f);

private:
    struct RegisteredWork
    {
        uint64_t Token = 0;
        FrameWorkStage Stage = FrameWorkStage::Visibility;
        concurrency::TaskPriority Priority = concurrency::TaskPriority::High;
        FrameWorkCallback Callback;
    };

    struct CachedMeshBounds
    {
        std::weak_ptr<MeshData> Owner;
        const void* VertexData = nullptr;
        size_t VertexCount = 0;
        uint64_t Revision = 0;
        AxisAlignedBounds Bounds;
    };

    std::vector<concurrency::TaskHandle> DispatchFrameWork(
        const FrameWorkContext& context, FrameWorkStage first, FrameWorkStage second);
    void WaitFor(std::vector<concurrency::TaskHandle>& handles);

    concurrency::TaskSystem* m_tasks = nullptr;
    std::vector<RegisteredWork> m_registeredWork;
    uint64_t m_nextWorkToken = 1;
    std::unordered_map<const MeshData*, CachedMeshBounds> m_boundsCache;
    SceneBatcher m_batcher;
    RenderFrameData m_frame;
};

} // namespace engine
