#include "engine/render/OcclusionCulling.h"

#include "engine/core/Camera.h"
#include "engine/render/SceneRenderer.h"
#include "engine/scene/RenderSettings.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace engine
{
namespace
{

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashWord(uint64_t& hash, uint32_t word) noexcept
{
    hash ^= word;
    hash *= kFnvPrime;
}

void HashValue(uint64_t& hash, uint64_t value) noexcept
{
    HashWord(hash, static_cast<uint32_t>(value));
    HashWord(hash, static_cast<uint32_t>(value >> 32u));
}

} // namespace

uint64_t HashOcclusionTransform(const glm::mat4& transform) noexcept
{
    uint64_t hash = kFnvOffset;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            HashWord(hash, std::bit_cast<uint32_t>(transform[column][row]));
    return hash;
}

OcclusionQueryRecord MakeOcclusionQueryRecord(
    const PreparedRenderCommand& command) noexcept
{
    OcclusionQueryRecord result;
    result.InstanceIndex = command.InstanceIndex;
    const MeshInstance& instance = *command.Source;
    result.Key = instance.TemporalId != 0
        ? (instance.TemporalId ^ 0x8000000000000000ull)
        : static_cast<uint64_t>(command.InstanceIndex) + 1ull;
    result.TransformHash = HashOcclusionTransform(instance.Transform);
    return result;
}

GpuOcclusionBounds MakeGpuOcclusionBounds(const PreparedRenderCommand& command,
                                           float inflation) noexcept
{
    const glm::vec3 extents = command.WorldBounds.Extents();
    const glm::vec3 padding = glm::max(extents * std::max(inflation, 0.0f),
                                       glm::vec3(0.0025f));
    return {glm::vec4(command.WorldBounds.Minimum - padding, 0.0f),
            glm::vec4(command.WorldBounds.Maximum + padding, 0.0f)};
}

OcclusionFrameSignature BuildOcclusionFrameSignature(
    const RenderFrameData& frame) noexcept
{
    OcclusionFrameSignature result;
    result.SceneIdentity = frame.SceneData;
    result.CameraPosition = frame.CameraData->Position;
    result.CameraForward = frame.CameraData->Forward();
    result.CameraFovDegrees = frame.CameraData->FovDegrees;
    uint64_t revision = kFnvOffset;
    HashValue(revision, frame.RenderCommands.size());
    for (const PreparedRenderCommand& command : frame.RenderCommands)
    {
        const OcclusionQueryRecord record = MakeOcclusionQueryRecord(command);
        HashValue(revision, record.Key);
    }
    result.SceneRevision = revision;
    return result;
}

bool TemporalOcclusionState::SignatureChanged(
    const OcclusionFrameSignature& signature,
    const VisibilitySettings& settings) const
{
    if (!m_hasSignature || signature.SceneIdentity != m_signature.SceneIdentity ||
        signature.SceneRevision != m_signature.SceneRevision)
        return true;
    const float positionThreshold = std::max(
        settings.OcclusionCameraPositionThreshold, 0.0f);
    const glm::vec3 cameraDelta = signature.CameraPosition - m_signature.CameraPosition;
    if (glm::dot(cameraDelta, cameraDelta) > positionThreshold * positionThreshold)
        return true;
    const float rotationThreshold = glm::radians(std::max(
        settings.OcclusionCameraRotationThresholdDeg, 0.0f));
    const float directionDot = glm::dot(glm::normalize(signature.CameraForward),
                                        glm::normalize(m_signature.CameraForward));
    if (directionDot < std::cos(rotationThreshold))
        return true;
    return std::abs(signature.CameraFovDegrees - m_signature.CameraFovDegrees) > 0.01f;
}

bool TemporalOcclusionState::BeginFrame(
    const OcclusionFrameSignature& signature,
    const VisibilitySettings& settings)
{
    ++m_frameIndex;
    const bool requested = settings.Enabled && settings.GpuOcclusionCulling;
    const bool changed = requested && SignatureChanged(signature, settings);
    const bool reset = changed || requested != m_active;
    if (reset)
    {
        m_history.clear();
        ++m_generation;
        m_signature = signature;
    }
    m_active = requested;
    m_confirmationFrames = std::max(settings.OcclusionConfirmationFrames, 2u);
    m_maxHiddenFrames = std::max(settings.OcclusionMaxHiddenFrames, 1u);
    m_hasSignature = requested;
    return reset;
}

void TemporalOcclusionState::ApplyResults(
    uint64_t generation, std::span<const OcclusionQueryRecord> records,
    std::span<const uint32_t> visibleBits)
{
    if (!m_active || generation != m_generation)
        return;
    const size_t count = std::min(records.size(), visibleBits.size());
    for (size_t index = 0; index < count; ++index)
    {
        const OcclusionQueryRecord& record = records[index];
        HistoryEntry& entry = m_history[record.Key];
        if (entry.TransformHash != record.TransformHash)
        {
            entry = {};
            entry.TransformHash = record.TransformHash;
        }
        entry.LastResultFrame = m_frameIndex;
        if (visibleBits[index] != 0)
        {
            entry.ConsecutiveOccluded = 0;
            entry.HiddenFrames = 0;
        }
        else
        {
            entry.ConsecutiveOccluded = std::min(
                entry.ConsecutiveOccluded + 1u, m_confirmationFrames);
        }
    }
}

bool TemporalOcclusionState::ShouldDraw(const OcclusionQueryRecord& record)
{
    if (!m_active)
        return true;
    const auto found = m_history.find(record.Key);
    if (found == m_history.end())
        return true;
    HistoryEntry& entry = found->second;
    if (entry.TransformHash != record.TransformHash)
    {
        m_history.erase(found);
        return true;
    }
    if (entry.ConsecutiveOccluded < m_confirmationFrames)
        return true;
    if (entry.HiddenFrames >= m_maxHiddenFrames)
    {
        entry.ConsecutiveOccluded = 0;
        entry.HiddenFrames = 0;
        return true;
    }
    ++entry.HiddenFrames;
    return false;
}

void TemporalOcclusionState::Reset()
{
    m_history.clear();
    ++m_generation;
    m_active = false;
    m_hasSignature = false;
}

} // namespace engine
