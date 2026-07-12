#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "engine/scene/Mesh.h"
#include "engine/scene/Material.h"
#include "engine/scene/Lights.h"
#include "engine/scene/RenderSettings.h"

namespace engine {

struct MeshInstance
{
    std::shared_ptr<MeshData> Mesh;
    Material Mat;
    glm::mat4 Transform{1.0f};
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

    DirectionalLight Sun;
    SkySettings Sky;
    PostProcessSettings PostProcess;
    FogSettings Fog;
    ShadowSettings Shadows;

    void AddInstance(std::shared_ptr<MeshData> mesh, const Material& mat, const glm::mat4& transform);
    void AddPointLight(const PointLight& light);
    void AddSpotLight(const SpotLight& light);

private:
    std::vector<MeshInstance> m_instances;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;
};

} // namespace engine
