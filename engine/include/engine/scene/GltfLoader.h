#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#include <glm/mat4x4.hpp>

namespace engine {

class Scene;

class GltfLoadError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct GltfLoadResult
{
    size_t Nodes = 0;
    size_t Meshes = 0;
    size_t Primitives = 0;
    size_t Materials = 0;
    size_t Textures = 0;
};

// Loads the default glTF scene and appends its renderable primitives to Scene.
// Node parent/child transforms are evaluated recursively before extraction.
GltfLoadResult LoadGltfScene(const std::string& path, Scene& scene, const glm::mat4& rootTransform = glm::mat4(1.0f));

} // namespace engine
