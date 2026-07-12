#include "engine/scene/Scene.h"

namespace engine {

void Scene::AddInstance(std::shared_ptr<MeshData> mesh, const Material& mat, const glm::mat4& transform)
{
    m_instances.push_back(MeshInstance{std::move(mesh), mat, transform});
}

void Scene::AddPointLight(const PointLight& light)
{
    m_pointLights.push_back(light);
}

void Scene::AddSpotLight(const SpotLight& light)
{
    m_spotLights.push_back(light);
}

} // namespace engine
