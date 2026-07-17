#include "engine/render/SceneRenderer.h"

#include "engine/core/Camera.h"
#include "engine/render/Instancing.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>

namespace engine {

namespace {

float TonemapScalar(float value, const PostProcessSettings& post)
{
    const float numerator = value * (post.ShoulderStrength * value
                          + post.LinearStrength * post.LinearAngle)
                          + post.ToeNumerator * post.ToeStrength;
    const float denominator = value * (post.ShoulderStrength * value + post.LinearStrength)
                            + post.ToeDenominator * post.ToeStrength;
    return numerator / denominator - post.ToeNumerator / post.ToeDenominator;
}

glm::vec3 SafeUp(const glm::vec3& direction, const glm::vec3& requested)
{
    const glm::vec3 normalizedDirection = glm::normalize(direction);
    glm::vec3 up = requested;
    if (glm::length(up) < 0.001f
        || std::abs(glm::dot(glm::normalize(up), normalizedDirection)) > 0.98f)
    {
        up = std::abs(normalizedDirection.y) > 0.98f
            ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return glm::normalize(up - normalizedDirection * glm::dot(up, normalizedDirection));
}

glm::mat4 ProjectedLightMatrix(const glm::vec3& position, const glm::vec3& direction,
                               float outerAngle, float range, float aspect = 1.0f)
{
    const glm::vec3 normalizedDirection = glm::normalize(direction);
    const glm::vec3 up = SafeUp(normalizedDirection, {0.0f, 1.0f, 0.0f});
    return glm::perspective(glm::radians(glm::clamp(outerAngle * 2.0f, 1.0f, 175.0f)),
                            aspect, 0.1f, range)
         * glm::lookAt(position, position + normalizedDirection, up);
}

void AppendIconLines(const DebugIcon& icon, const Camera& camera,
                     std::vector<DebugLine>& output)
{
    const glm::vec3 right = camera.Right() * icon.Size;
    const glm::vec3 up = glm::normalize(glm::cross(camera.Right(), camera.Forward())) * icon.Size;
    const auto line = [&](const glm::vec3& a, const glm::vec3& b) {
        output.push_back({icon.Position + a, icon.Position + b,
                          icon.Color, icon.Depth});
    };
    if (icon.Type == DebugIconType::SpotLight)
    {
        line(-right, -up); line(-up, right); line(right, -right);
    }
    else if (icon.Type == DebugIconType::DirectionalLight)
    {
        line(-right, right); line(glm::vec3(0.0f), up);
        line(up, up * 0.55f - right * 0.3f);
        line(up, up * 0.55f + right * 0.3f);
    }
    else if (icon.Type == DebugIconType::Camera)
    {
        line(-right - up * 0.6f, right - up * 0.6f);
        line(right - up * 0.6f, right + up * 0.6f);
        line(right + up * 0.6f, -right + up * 0.6f);
        line(-right + up * 0.6f, -right - up * 0.6f);
        line(right * 0.2f + up * 0.6f, right * 0.75f + up);
        line(right * 0.75f + up, right + up * 0.6f);
    }
    else
    {
        line(-right, right); line(-up, up);
        if (icon.Type == DebugIconType::PointLight)
        {
            line(-right * 0.7f - up * 0.7f, right * 0.7f + up * 0.7f);
            line(-right * 0.7f + up * 0.7f, right * 0.7f - up * 0.7f);
        }
    }
}

} // namespace

uint64_t SceneRenderer::AddFrameWork(FrameWorkStage stage, FrameWorkCallback callback,
                                     concurrency::TaskPriority priority)
{
    if (!callback)
        return 0;
    const uint64_t token = m_nextWorkToken++;
    m_registeredWork.push_back({token, stage, priority, std::move(callback)});
    return token;
}

bool SceneRenderer::RemoveFrameWork(uint64_t token)
{
    const auto found = std::find_if(m_registeredWork.begin(), m_registeredWork.end(),
                                    [token](const RegisteredWork& work) { return work.Token == token; });
    if (found == m_registeredWork.end())
        return false;
    m_registeredWork.erase(found);
    return true;
}

void SceneRenderer::ClearFrameWork()
{
    m_registeredWork.clear();
}

std::vector<concurrency::TaskHandle> SceneRenderer::DispatchFrameWork(
    const FrameWorkContext& context, FrameWorkStage first, FrameWorkStage second)
{
    std::vector<concurrency::TaskHandle> handles;
    for (const RegisteredWork& work : m_registeredWork)
    {
        if (work.Stage != first && work.Stage != second)
            continue;
        handles.push_back(m_tasks->Submit(
            [callback = work.Callback, context](const concurrency::CancellationToken& cancellation) {
                callback(context, cancellation);
            }, work.Priority));
    }
    return handles;
}

void SceneRenderer::WaitFor(std::vector<concurrency::TaskHandle>& handles)
{
    for (const concurrency::TaskHandle& handle : handles)
    {
        m_tasks->Wait(handle);
        handle.RethrowIfFailed();
    }
}

const RenderFrameData& SceneRenderer::PrepareFrame(const Scene& scene, const Camera& camera,
                                                    int width, int height,
                                                    const debug::DebugOverlayImage* debugOverlay,
                                                    float timeSeconds,
                                                    float deltaSeconds)
{
    if (!m_tasks)
        m_tasks = &concurrency::TaskSystem::Global();
    const uint64_t nextFrameIndex = m_frame.FrameIndex + 1;
    m_frame = {};
    m_frame.SceneData = &scene;
    m_frame.CameraData = &camera;
    m_frame.Width = std::max(width, 1);
    m_frame.Height = std::max(height, 1);
    m_frame.AspectRatio = static_cast<float>(m_frame.Width) / static_cast<float>(m_frame.Height);
    m_frame.FrameIndex = nextFrameIndex;
    m_frame.TimeSeconds = timeSeconds;
    m_frame.DeltaSeconds = std::max(deltaSeconds, 0.0f);
    m_frame.DebugOverlay = debugOverlay;
    m_frame.View = camera.GetView();
    m_frame.BaseProjection = camera.GetProjection(m_frame.AspectRatio);

    const FrameWorkContext workContext{scene, camera, m_frame.Width, m_frame.Height,
                                       m_frame.TimeSeconds, m_frame.DeltaSeconds};
    std::vector<concurrency::TaskHandle> simulationWork = DispatchFrameWork(
        workContext, FrameWorkStage::Animation, FrameWorkStage::Particles);
    WaitFor(simulationWork);

    m_frame.SunDirection = glm::normalize(scene.Sun.Direction);
    const float elevation = glm::degrees(std::asin(glm::clamp(-m_frame.SunDirection.y, -1.0f, 1.0f)));
    m_frame.ProceduralDayNight = scene.Environment.Source == EnvironmentSource::ProceduralSky
                              && scene.Sky.EnableDayNightCycle;
    m_frame.DayNight = m_frame.ProceduralDayNight ? EvaluateDayNight(elevation) : DayNightState{};
    m_frame.EffectiveSunColor = scene.Sun.Color * m_frame.DayNight.SunTint
                              * (scene.Sun.Intensity * m_frame.DayNight.DirectSunAmount);
    m_frame.SunShadowsActive = scene.Sun.CastsShadows && scene.Sun.Intensity > 0.0f
                            && m_frame.DayNight.DirectSunAmount > 0.001f;

    std::vector<concurrency::TaskHandle> preparationWork = DispatchFrameWork(
        workContext, FrameWorkStage::Visibility, FrameWorkStage::Visibility);
    const uint32_t customTaskCount = static_cast<uint32_t>(
        simulationWork.size() + preparationWork.size());
    preparationWork.push_back(m_tasks->Submit([this, &scene, &camera](const concurrency::CancellationToken&) {
        CascadeShadowConfig cascadeConfig;
        cascadeConfig.MaxDistance = scene.Shadows.MaxDistance;
        cascadeConfig.SplitLambda = scene.Shadows.CascadeSplitLambda;
        cascadeConfig.BlendFraction = scene.Shadows.CascadeBlendFraction;
        m_frame.Cascades = BuildCascadeShadows(
            camera, m_frame.AspectRatio, m_frame.SunDirection, cascadeConfig);
    }, concurrency::TaskPriority::Critical));

    // The local-light arrays are deliberately small and bounded. Preparing
    // them inline is cheaper and more deterministic than allocating three
    // jobs and immediately joining them, while the heavier cascade build
    // above still overlaps this CPU work on a worker.
    for (const PointLight& light : scene.PointLights())
    {
        if (!light.Enabled || m_frame.LocalLights.PointCount >= PreparedLocalLights::MaxPointLights)
            continue;
        m_frame.LocalLights.Points[m_frame.LocalLights.PointCount++].Source = &light;
    }
    for (const SpotLight& light : scene.SpotLights())
    {
        if (!light.Enabled || m_frame.LocalLights.SpotCount >= PreparedLocalLights::MaxSpotLights)
            continue;
        PreparedSpotLight& prepared = m_frame.LocalLights.Spots[m_frame.LocalLights.SpotCount++];
        prepared.Source = &light;
        prepared.Direction = glm::normalize(light.Direction);
        prepared.Projection = ProjectedLightMatrix(
            light.Position, prepared.Direction, light.OuterConeDeg, light.Range);
    }
    for (const AreaLight& light : scene.AreaLights())
    {
        if (!light.Enabled || m_frame.LocalLights.AreaCount >= PreparedLocalLights::MaxAreaLights)
            continue;
        PreparedAreaLight& prepared = m_frame.LocalLights.Areas[m_frame.LocalLights.AreaCount++];
        prepared.Source = &light;
        prepared.Direction = glm::normalize(light.Direction);
        prepared.Up = SafeUp(prepared.Direction, light.Up);
        prepared.Right = glm::normalize(glm::cross(prepared.Direction, prepared.Up));
        const float aspect = std::max(light.Size.x / std::max(light.Size.y, 0.01f), 0.01f);
        prepared.Projection = ProjectedLightMatrix(
            light.Position, prepared.Direction, light.BarnAngleDeg, light.Range, aspect);
    }

    const SceneBatchBuildResult& batchBuild = m_batcher.Build(scene, m_frame.FrameIndex);
    m_frame.Batching = batchBuild.Statistics;
    const std::vector<const MeshInstance*>& instances = batchBuild.Instances;
    if ((m_frame.FrameIndex % 120u) == 0u)
    {
        std::erase_if(m_boundsCache, [](const auto& entry) {
            return entry.second.Owner.expired();
        });
    }

    std::vector<AxisAlignedBounds> localBounds(instances.size());
    for (size_t index = 0; index < instances.size(); ++index)
    {
        const std::shared_ptr<MeshData>& mesh = instances[index]->Mesh;
        if (!mesh || mesh->Vertices.empty() || mesh->Indices.empty())
            continue;
        CachedMeshBounds& cached = m_boundsCache[mesh.get()];
        const std::shared_ptr<MeshData> cachedOwner = cached.Owner.lock();
        if (cachedOwner.get() != mesh.get() || cached.VertexData != mesh->Vertices.data() ||
            cached.VertexCount != mesh->Vertices.size() ||
            cached.Revision != mesh->Revision)
        {
            cached.Owner = mesh;
            cached.VertexData = mesh->Vertices.data();
            cached.VertexCount = mesh->Vertices.size();
            cached.Revision = mesh->Revision;
            cached.Bounds = ComputeMeshBounds(*mesh);
        }
        localBounds[index] = cached.Bounds;
    }

    struct ClassifiedCommand
    {
        PreparedRenderCommand Command;
        AxisAlignedBounds Bounds;
        VisibilityClassification Classification = VisibilityClassification::InvalidBounds;
        bool Render = false;
        bool Shadow = false;
    };
    std::vector<ClassifiedCommand> classified(instances.size());
    const ViewFrustum frustum = ExtractViewFrustum(m_frame.BaseProjection * m_frame.View);
    const float globalDistance = scene.Visibility.MaxDistance > 0.0f
        ? std::min(scene.Visibility.MaxDistance, camera.FarPlane) : camera.FarPlane;
    m_tasks->ParallelFor(instances.size(), 32,
        [&instances, &localBounds, &classified](size_t index) {
            const MeshInstance& instance = *instances[index];
            ClassifiedCommand& result = classified[index];
            result.Command.InstanceIndex = static_cast<uint32_t>(index);
            if (!instance.Mesh || instance.Mesh->Vertices.empty() || instance.Mesh->Indices.empty())
                return;
            result.Command.Source = &instance;
            result.Command.IndexCount = static_cast<uint32_t>(instance.Mesh->Indices.size());
            result.Bounds = TransformBounds(localBounds[index], instance.Transform);
            result.Command.WorldBounds = result.Bounds;
            if (!result.Bounds.Valid)
                return;
            result.Classification = VisibilityClassification::Visible;
        });

    struct HismGroupKey
    {
        const MeshData* Mesh = nullptr;
        uint64_t ExplicitGroup = 0;
        uint32_t DistanceBits = 0;
        bool operator==(const HismGroupKey&) const = default;
    };
    struct HismGroupKeyHash
    {
        size_t operator()(const HismGroupKey& key) const noexcept
        {
            size_t hash = std::hash<const MeshData*>{}(key.Mesh);
            hash ^= std::hash<uint64_t>{}(key.ExplicitGroup) +
                    0x9e3779b9u + (hash << 6u) + (hash >> 2u);
            hash ^= std::hash<uint32_t>{}(key.DistanceBits) +
                    0x9e3779b9u + (hash << 6u) + (hash >> 2u);
            return hash;
        }
    };
    struct HismGroup
    {
        float MaxDistance = 0.0f;
        std::vector<size_t> Members;
    };

    std::vector<uint8_t> handledByHierarchy(instances.size(), 0);
    std::vector<HismGroup> hismGroups;
    if (scene.Visibility.Enabled && scene.Instancing.HierarchicalCulling)
    {
        std::unordered_map<HismGroupKey, size_t, HismGroupKeyHash> groupLookup;
        for (size_t index = 0; index < instances.size(); ++index)
        {
            const MeshInstance& instance = *instances[index];
            if (!classified[index].Bounds.Valid || instance.AlwaysVisible ||
                !instance.AllowInstancing)
                continue;
            float maxDistance = globalDistance;
            if (instance.MaxDrawDistance > 0.0f)
                maxDistance = std::min(maxDistance, instance.MaxDrawDistance);
            const HismGroupKey key{
                instance.HismGroupId == 0 ? instance.Mesh.get() : nullptr,
                instance.HismGroupId, std::bit_cast<uint32_t>(maxDistance)};
            const auto [found, inserted] = groupLookup.emplace(key, hismGroups.size());
            if (inserted)
                hismGroups.push_back({maxDistance, {}});
            hismGroups[found->second].Members.push_back(index);
        }
        const size_t minimumGroupSize = std::max<size_t>(
            scene.Instancing.HismMinimumGroupSize,
            std::max(scene.Instancing.HismLeafSize, 1u) * 2u);
        std::erase_if(hismGroups, [minimumGroupSize](const HismGroup& group) {
            return group.Members.size() < minimumGroupSize;
        });
        for (const HismGroup& group : hismGroups)
            for (size_t index : group.Members)
                handledByHierarchy[index] = 1;
    }

    std::vector<HismCullStatistics> hismStatistics(hismGroups.size());
    m_tasks->ParallelFor(hismGroups.size(), 1,
        [&hismGroups, &hismStatistics, &classified, &frustum, &camera,
         &scene](size_t groupIndex) {
            const HismGroup& group = hismGroups[groupIndex];
            std::vector<HismCullItem> items;
            items.reserve(group.Members.size());
            for (size_t index : group.Members)
                items.push_back({static_cast<uint32_t>(index), classified[index].Bounds});
            const std::vector<VisibilityClassification> results =
                CullHierarchicalInstances(items, frustum, camera.Position,
                    group.MaxDistance, scene.Visibility.FrustumCulling,
                    scene.Visibility.DistanceCulling,
                    scene.Instancing.HismLeafSize, &hismStatistics[groupIndex]);
            for (size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
                classified[items[itemIndex].InstanceIndex].Classification = results[itemIndex];
        });

    m_tasks->ParallelFor(instances.size(), 32,
        [&instances, &scene, &camera, &frustum, &classified,
         &handledByHierarchy, globalDistance](size_t index) {
            ClassifiedCommand& result = classified[index];
            if (!result.Bounds.Valid || handledByHierarchy[index] != 0)
                return;
            const MeshInstance& instance = *instances[index];
            if (scene.Visibility.Enabled && !instance.AlwaysVisible)
            {
                float maxDistance = globalDistance;
                if (instance.MaxDrawDistance > 0.0f)
                    maxDistance = std::min(maxDistance, instance.MaxDrawDistance);
                if (scene.Visibility.DistanceCulling && maxDistance > 0.0f &&
                    SquaredDistanceToBounds(camera.Position, result.Bounds) > maxDistance * maxDistance)
                {
                    result.Classification = VisibilityClassification::DistanceCulled;
                    return;
                }
                if (scene.Visibility.FrustumCulling && !IntersectsFrustum(frustum, result.Bounds))
                    result.Classification = VisibilityClassification::FrustumCulled;
            }
        });

    m_tasks->ParallelFor(instances.size(), 32,
        [&instances, &classified](size_t index) {
            ClassifiedCommand& result = classified[index];
            if (!result.Bounds.Valid)
                return;
            result.Render = result.Classification == VisibilityClassification::Visible;
            // Nearby off-frustum casters remain available. Distance culling
            // suppresses both camera and shadow submissions as before.
            result.Shadow = instances[index]->CastsShadows &&
                result.Classification != VisibilityClassification::DistanceCulled;
        });

    m_frame.Visibility.HismGroups = static_cast<uint32_t>(hismGroups.size());
    for (const HismCullStatistics& statistics : hismStatistics)
    {
        m_frame.Visibility.HismNodesTested += statistics.NodesTested;
        m_frame.Visibility.HismNodesCulled += statistics.NodesCulled;
        m_frame.Visibility.HismInstancesCulled += statistics.InstancesCulled;
        m_frame.Visibility.HismLeafTests += statistics.LeafTests;
    }

    m_frame.RenderCommands.reserve(instances.size());
    m_frame.ShadowCommands.reserve(instances.size());
    if (scene.Visibility.DebugBounds || scene.Visibility.DebugOcclusion)
        m_frame.VisibilityDebug.reserve(instances.size());
    for (const ClassifiedCommand& result : classified)
    {
        ++m_frame.Visibility.Tested;
        switch (result.Classification)
        {
        case VisibilityClassification::Visible:
            if (result.Render)
            {
                m_frame.RenderCommands.push_back(result.Command);
                ++m_frame.Visibility.Visible;
            }
            break;
        case VisibilityClassification::FrustumCulled:
            ++m_frame.Visibility.FrustumCulled;
            break;
        case VisibilityClassification::DistanceCulled:
            ++m_frame.Visibility.DistanceCulled;
            break;
        case VisibilityClassification::InvalidBounds:
            ++m_frame.Visibility.InvalidBounds;
            break;
        }
        if (result.Shadow)
        {
            m_frame.ShadowCommands.push_back(result.Command);
            ++m_frame.Visibility.ShadowCasters;
        }
        if ((scene.Visibility.DebugBounds || scene.Visibility.DebugOcclusion) &&
            result.Bounds.Valid &&
            (scene.Visibility.DebugCulledBounds ||
             result.Classification == VisibilityClassification::Visible))
        {
            m_frame.VisibilityDebug.push_back(
                {result.Bounds, result.Classification, result.Command.InstanceIndex});
        }
    }
    WaitFor(preparationWork);

    const float whitePointValue = TonemapScalar(scene.PostProcess.WhitePoint, scene.PostProcess);
    m_frame.TonemapWhitePointScale = 1.0f / std::max(whitePointValue, 1.0e-6f);
    m_frame.Preparation.WorkerCount = m_tasks->WorkerCount();
    m_frame.Preparation.CustomTaskCount = customTaskCount;
    m_frame.Preparation.RenderCommandCount = static_cast<uint32_t>(m_frame.RenderCommands.size());
    m_frame.Preparation.ShadowCommandCount = static_cast<uint32_t>(m_frame.ShadowCommands.size());
    m_frame.DebugLines = scene.DebugDraw().Lines();
    for (const DebugIcon& icon : scene.DebugDraw().Icons())
        AppendIconLines(icon, camera, m_frame.DebugLines);
    if (scene.SelectedEntity != 0)
    {
        for (const MeshInstance& instance : scene.Instances())
        {
            if (instance.SourceEntity != scene.SelectedEntity || !instance.Mesh)
                continue;
            CachedMeshBounds& cached = m_boundsCache[instance.Mesh.get()];
            const std::shared_ptr<MeshData> cachedOwner = cached.Owner.lock();
            if (cachedOwner.get() != instance.Mesh.get() ||
                cached.VertexData != instance.Mesh->Vertices.data() ||
                cached.VertexCount != instance.Mesh->Vertices.size() ||
                cached.Revision != instance.Mesh->Revision)
            {
                cached.Owner = instance.Mesh;
                cached.VertexData = instance.Mesh->Vertices.data();
                cached.VertexCount = instance.Mesh->Vertices.size();
                cached.Revision = instance.Mesh->Revision;
                cached.Bounds = ComputeMeshBounds(*instance.Mesh);
            }
            const AxisAlignedBounds selectedBounds = TransformBounds(
                cached.Bounds, instance.Transform);
            if (!selectedBounds.Valid)
                continue;
            DebugDrawList selected;
            selected.Aabb(selectedBounds.Minimum,
                          selectedBounds.Maximum,
                          scene.SelectionColor, DebugDepthMode::Overlay);
            m_frame.DebugLines.insert(m_frame.DebugLines.end(),
                                      selected.Lines().begin(), selected.Lines().end());
        }
    }
    return m_frame;
}

bool SceneRenderer::RenderViewport(IRenderBackend& backend,
                                   RenderViewportHandle viewport,
                                   const Scene& scene, const Camera& camera,
                                   uint32_t width, uint32_t height,
                                   float timeSeconds, float deltaSeconds)
{
    if (!viewport || width == 0 || height == 0)
        return false;
    const RenderFrameData& frame = PrepareFrame(
        scene, camera, static_cast<int>(width), static_cast<int>(height), nullptr,
        timeSeconds, deltaSeconds);
    return backend.RenderViewport(viewport, frame);
}

} // namespace engine
