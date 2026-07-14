#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>

#include <glm/glm.hpp>

namespace engine
{

struct PreparedRenderCommand;
struct RenderFrameData;
struct VisibilitySettings;

// std430-compatible payload shared by the OpenGL and Vulkan compute paths.
struct GpuOcclusionBounds
{
    glm::vec4 Minimum{0.0f};
    glm::vec4 Maximum{0.0f};
};
static_assert(sizeof(GpuOcclusionBounds) == 32);

struct OcclusionQueryRecord
{
    uint64_t Key = 0;
    uint64_t TransformHash = 0;
    uint32_t InstanceIndex = 0;
};

struct OcclusionFrameSignature
{
    const void* SceneIdentity = nullptr;
    glm::vec3 CameraPosition{0.0f};
    glm::vec3 CameraForward{0.0f, 0.0f, -1.0f};
    float CameraFovDegrees = 60.0f;
    uint64_t SceneRevision = 0;
};

uint64_t HashOcclusionTransform(const glm::mat4& transform) noexcept;
OcclusionQueryRecord MakeOcclusionQueryRecord(const PreparedRenderCommand& command) noexcept;
GpuOcclusionBounds MakeGpuOcclusionBounds(const PreparedRenderCommand& command,
                                           float inflation) noexcept;
OcclusionFrameSignature BuildOcclusionFrameSignature(const RenderFrameData& frame) noexcept;

// Backend-neutral temporal policy. Native backends only produce raw visible
// bits; confirmation, stale-result rejection and conservative reset behavior
// remain identical across OpenGL and Vulkan.
class TemporalOcclusionState
{
public:
    bool BeginFrame(const OcclusionFrameSignature& signature,
                    const VisibilitySettings& settings);
    void ApplyResults(uint64_t generation,
                      std::span<const OcclusionQueryRecord> records,
                      std::span<const uint32_t> visibleBits);
    bool ShouldDraw(const OcclusionQueryRecord& record);
    void Reset();

    [[nodiscard]] bool Active() const noexcept { return m_active; }
    [[nodiscard]] uint64_t Generation() const noexcept { return m_generation; }
    [[nodiscard]] uint64_t FrameIndex() const noexcept { return m_frameIndex; }

private:
    struct HistoryEntry
    {
        uint64_t TransformHash = 0;
        uint64_t LastResultFrame = 0;
        uint32_t ConsecutiveOccluded = 0;
        uint32_t HiddenFrames = 0;
    };

    bool SignatureChanged(const OcclusionFrameSignature& signature,
                          const VisibilitySettings& settings) const;

    std::unordered_map<uint64_t, HistoryEntry> m_history;
    OcclusionFrameSignature m_signature{};
    uint64_t m_generation = 1;
    uint64_t m_frameIndex = 0;
    uint32_t m_confirmationFrames = 2;
    uint32_t m_maxHiddenFrames = 30;
    bool m_active = false;
    bool m_hasSignature = false;
};

} // namespace engine
