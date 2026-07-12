#pragma once

#include <glm/glm.hpp>

namespace engine {

class Window;
class Scene;
class Camera;

// Shared contract between the OpenGL and Vulkan backends. Kept intentionally
// small (immediate-mode-ish per-frame calls) rather than a full generic RHI
// (command buffers, pipeline objects, descriptor abstractions, ...) because
// building that abstraction well requires knowing both backends' real
// constraints first. Once the Vulkan backend is fleshed out and its actual
// resource lifetime/threading needs are clear, this is the seam to widen into
// a proper RHI (buffers/textures/pipelines as first-class objects shared by
// both backends) instead of guessing at one up front.
class IRenderBackend
{
public:
    virtual ~IRenderBackend() = default;

    virtual void Init(Window& window) = 0;
    virtual void Shutdown() = 0;

    virtual void Resize(int width, int height) = 0;

    virtual void RenderFrame(const Scene& scene, const Camera& camera) = 0;

    virtual const char* Name() const = 0;
};

} // namespace engine
