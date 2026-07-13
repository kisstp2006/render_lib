#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "engine/render/CascadedShadows.h"
#include "engine/scene/Environment.h"

namespace engine {

class Camera;
class Scene;
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

    glm::mat4 View{1.0f};
    glm::mat4 BaseProjection{1.0f};
    glm::vec3 SunDirection{-0.4f, -0.85f, -0.35f};
    glm::vec3 EffectiveSunColor{1.0f};
    DayNightState DayNight{};
    bool ProceduralDayNight = false;
    bool SunShadowsActive = false;
    CascadeShadowData Cascades{};
    PreparedLocalLights LocalLights{};
    float TonemapWhitePointScale = 1.0f;
    const debug::DebugOverlayImage* DebugOverlay = nullptr;
};

// Shared scene-rendering front end. It owns no API objects; native buffers,
// textures, command recording and pipelines remain in the selected backend.
class SceneRenderer
{
public:
    const RenderFrameData& PrepareFrame(const Scene& scene, const Camera& camera,
                                        int width, int height,
                                        const debug::DebugOverlayImage* debugOverlay = nullptr);

private:
    RenderFrameData m_frame;
};

} // namespace engine
