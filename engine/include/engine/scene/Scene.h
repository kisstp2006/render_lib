#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/scene/Mesh.h"

namespace engine {

// Scalar PBR parameters in the metallic/roughness convention, matching the
// Source 2 vr_standard/complex material model closely enough for this engine's
// purposes (see pbr.slang: SpecularColor/AlbedoColor/Roughness terms).
struct Material
{
    glm::vec3 Albedo{0.8f, 0.8f, 0.8f};
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    glm::vec3 Emissive{0.0f};
    float AmbientOcclusion = 1.0f;
    // Source materials default specular to a flat 0.04 (4%) dielectric F0,
    // exposed here in case a material wants a non-standard value (e.g. skin, wax).
    float SpecularF0 = 0.04f;
};

struct DirectionalLight
{
    glm::vec3 Direction{-0.4f, -0.85f, -0.35f};
    glm::vec3 Color{1.0f, 0.96f, 0.88f};
    float Intensity = 3.0f;
    bool CastsShadows = true;
};

struct PointLight
{
    glm::vec3 Position{0.0f};
    glm::vec3 Color{1.0f};
    float Intensity = 20.0f;
    float Radius = 15.0f;
};

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

    DirectionalLight Sun;
    glm::vec3 AmbientColor{0.35f, 0.38f, 0.45f}; // cool sky-ish ambient, Source-map-like

    void AddInstance(std::shared_ptr<MeshData> mesh, const Material& mat, const glm::mat4& transform);
    void AddPointLight(const PointLight& light);

private:
    std::vector<MeshInstance> m_instances;
    std::vector<PointLight> m_pointLights;
};

} // namespace engine
