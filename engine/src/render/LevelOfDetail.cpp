#include "engine/render/LevelOfDetail.h"

#include "engine/core/Camera.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {
namespace {

float ProjectedDiameter(const AxisAlignedBounds& bounds, const Camera& camera,
                        uint32_t viewportHeight)
{
    if (!bounds.Valid || viewportHeight == 0)
        return 0.0f;
    const float radius = glm::length(bounds.Extents());
    if (radius <= 1.0e-6f)
        return 0.0f;
    if (camera.Projection == CameraProjection::Orthographic)
    {
        const float height = std::max(camera.OrthographicSize, 1.0e-4f);
        return radius * 2.0f * static_cast<float>(viewportHeight) / height;
    }
    const float viewDepth = glm::dot(bounds.Center() - camera.Position, camera.Forward());
    if (viewDepth <= 1.0e-4f)
        return std::numeric_limits<float>::max();
    const float halfFov = glm::radians(std::max(camera.FovDegrees, 1.0f)) * 0.5f;
    return radius * 2.0f * static_cast<float>(viewportHeight) /
           (2.0f * std::tan(halfFov) * viewDepth);
}

float RelativeError(const MeshLodLevel& level)
{
    if (std::isfinite(level.RelativeError) && level.RelativeError > 0.0f)
        return level.RelativeError;
    // Authored chains sometimes only expose a triangle ratio. This conservative
    // fallback is deterministic and keeps such chains useful until an offline
    // cooker provides a measured geometric error.
    return std::sqrt(std::max(0.0f, 1.0f - glm::clamp(level.TriangleRatio, 0.0f, 1.0f))) * 0.15f;
}

} // namespace

LodSelection LodSelector::Select(uint64_t temporalId, uint64_t frameIndex,
                                  const AxisAlignedBounds& worldBounds,
                                  const Camera& camera, uint32_t viewportHeight,
                                  std::span<const MeshLodLevel> levels,
                                  const LodSettings& settings,
                                  LodStatistics* statistics)
{
    LodSelection result;
    result.ProjectedDiameterPixels = ProjectedDiameter(worldBounds, camera, viewportHeight);
    uint32_t maximumLevel = 0;
    for (const MeshLodLevel& level : levels)
    {
        if (!level.Mesh || level.Mesh->Vertices.empty() || level.Mesh->Indices.empty())
            break;
        ++maximumLevel;
    }
    if (!settings.Enabled || maximumLevel == 0)
        return result;

    const float target = std::max(0.01f, settings.TargetScreenSpaceErrorPixels) *
                         std::exp2(-settings.Bias);
    uint32_t desired = 0;
    for (uint32_t level = 1; level <= maximumLevel; ++level)
    {
        if (!levels[level - 1u].Mesh)
            break;
        const float error = result.ProjectedDiameterPixels * RelativeError(levels[level - 1u]);
        if (error <= target)
            desired = level;
        else
            break;
    }
    if (settings.ForcedLevel != UINT32_MAX)
        desired = std::min(settings.ForcedLevel, maximumLevel);

    // A zero ID explicitly means that the instance has no stable temporal
    // identity. Applying another anonymous instance's history would couple
    // unrelated objects, so those commands use the stateless desired level.
    if (temporalId == 0)
    {
        result.Level = desired;
        result.EstimatedErrorPixels = desired == 0 ? 0.0f
            : result.ProjectedDiameterPixels * RelativeError(levels[desired - 1u]);
        if (statistics)
        {
            ++statistics->Candidates;
            ++statistics->Selected[std::min(desired, 7u)];
        }
        return result;
    }

    History& history = m_history[temporalId];
    const uint32_t previous = std::min(history.Level, maximumLevel);
    uint32_t selected = desired;
    if (history.LastSeenFrame != 0 && settings.ForcedLevel == UINT32_MAX)
    {
        selected = previous;
        const float hysteresis = glm::clamp(settings.HysteresisFraction, 0.0f, 0.95f);
        // Receding: only accept a cheaper level after it is safely below the
        // error target. Approaching: only restore detail once the active level
        // visibly exceeds it.
        while (selected < desired)
        {
            const float error = result.ProjectedDiameterPixels * RelativeError(levels[selected]);
            if (error > target * (1.0f - hysteresis))
                break;
            ++selected;
        }
        while (selected > desired)
        {
            const float error = result.ProjectedDiameterPixels * RelativeError(levels[selected - 1u]);
            if (error <= target * (1.0f + hysteresis))
                break;
            --selected;
        }
    }
    const bool transitioned = history.LastSeenFrame != 0 && selected != previous;
    history.Level = selected;
    history.LastSeenFrame = frameIndex;
    if ((frameIndex % 240u) == 0u)
        std::erase_if(m_history, [frameIndex](const auto& entry) {
            return frameIndex > entry.second.LastSeenFrame &&
                   frameIndex - entry.second.LastSeenFrame > 240u;
        });

    result.Level = selected;
    result.EstimatedErrorPixels = selected == 0 ? 0.0f
        : result.ProjectedDiameterPixels * RelativeError(levels[selected - 1u]);
    if (statistics)
    {
        ++statistics->Candidates;
        ++statistics->Selected[std::min(selected, 7u)];
        if (transitioned)
            ++statistics->Transitions;
    }
    return result;
}

void LodSelector::Reset()
{
    m_history.clear();
}

} // namespace engine
