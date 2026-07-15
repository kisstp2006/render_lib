#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "engine/scene/Mesh.h"
#include "engine/scene/Material.h"
#include "engine/scene/Lights.h"
#include "engine/scene/Environment.h"
#include "engine/scene/RenderSettings.h"
#include "engine/scene/DebugDraw.h"

namespace engine {

struct MeshInstance
{
    uint64_t TemporalId = 0;
    // Stable behavior-world entity handle used by picking and editor
    // selection. Zero denotes a renderer-only instance.
    uint64_t SourceEntity = 0;
    std::shared_ptr<MeshData> Mesh;
    Material Mat;
    glm::mat4 Transform{1.0f};
    bool CastsShadows = true;
    bool AlwaysVisible = false;
    bool AllowInstancing = true;
    // Zero selects automatic grouping. A non-zero value lets callers keep
    // otherwise compatible spatial populations in separate HISM trees.
    uint64_t HismGroupId = 0;
    float MaxDrawDistance = 0.0f;
};

// A minimal scene container: no ECS, just flat lists. Enough for the sandbox
// and a reasonable starting point before a real scene graph is needed.
class Scene
{
public:
    std::vector<MeshInstance>& Instances() { return m_instances; }
    const std::vector<MeshInstance>& Instances() const { return m_instances; }

    std::vector<PointLight>& PointLights() { return m_pointLights; }
    const std::vector<PointLight>& PointLights() const { return m_pointLights; }

    std::vector<SpotLight>& SpotLights() { return m_spotLights; }
    const std::vector<SpotLight>& SpotLights() const { return m_spotLights; }

    std::vector<AreaLight>& AreaLights() { return m_areaLights; }
    const std::vector<AreaLight>& AreaLights() const { return m_areaLights; }

    DirectionalLight Sun;
    SkySettings Sky;
    EnvironmentSettings Environment;
    PostProcessSettings PostProcess;
    FogSettings Fog;
    ShadowSettings Shadows;
    VisibilitySettings Visibility;
    InstancingSettings Instancing;
    uint64_t SelectedEntity = 0;
    glm::vec3 SelectionColor{3.0f, 1.25f, 0.08f};

    DebugDrawList& DebugDraw() { return m_debugDraw; }
    const DebugDrawList& DebugDraw() const { return m_debugDraw; }

    void AddInstance(std::shared_ptr<MeshData> mesh, const Material& mat, const glm::mat4& transform);
    void AddPointLight(const PointLight& light);
    void AddSpotLight(const SpotLight& light);
    void AddAreaLight(const AreaLight& light);

private:
    uint64_t m_nextTemporalId = 1;
    std::vector<MeshInstance> m_instances;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;
    std::vector<AreaLight> m_areaLights;
    DebugDrawList m_debugDraw;
};

} // namespace engine
