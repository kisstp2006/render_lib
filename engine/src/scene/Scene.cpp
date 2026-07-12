#include "engine/scene/Scene.h"

namespace engine {

void Scene::AddInstance(std::shared_ptr<MeshData> mesh, const Material& mat, const glm::mat4& transform)
{
    MeshInstance instance;
    instance.TemporalId = m_nextTemporalId++;
    instance.Mesh = std::move(mesh);
    instance.Mat = mat;
    instance.Transform = transform;
    m_instances.push_back(std::move(instance));
}

void Scene::AddPointLight(const PointLight& light)
{
    m_pointLights.push_back(light);
}

void Scene::AddSpotLight(const SpotLight& light)
{
    m_spotLights.push_back(light);
}

void Scene::AddAreaLight(const AreaLight& light)
{
    m_areaLights.push_back(light);
}

} // namespace engine
