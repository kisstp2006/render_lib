#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include <glm/mat4x4.hpp>

namespace engine {
class Scene;
struct ColorGradingLutData;
struct HdrImageData;
}

// Development host for the same descriptor -> cook -> runtime-registry path
// used by packaged content. Every sample owns one instance for its full
// lifetime, while OpenGL and Vulkan continue to consume identical CPU data.
class SampleAssetPipeline
{
public:
    SampleAssetPipeline();
    ~SampleAssetPipeline();

    SampleAssetPipeline(const SampleAssetPipeline&) = delete;
    SampleAssetPipeline& operator=(const SampleAssetPipeline&) = delete;

    void AddStaticMesh(engine::Scene& scene, const std::filesystem::path& source,
                       const glm::mat4& rootTransform = glm::mat4(1.0f));
    std::shared_ptr<engine::HdrImageData> LoadEnvironment(const std::filesystem::path& source);
    std::shared_ptr<engine::ColorGradingLutData> LoadColorGrading(const std::filesystem::path& source);

    // Invalidates every primary resource imported by this sample host. The
    // caller can then rebuild its Scene to exercise the same descriptor ->
    // cook -> runtime-load path used by development hot reload.
    size_t InvalidateImportedResources();

    [[nodiscard]] size_t AssetCount() const;
    [[nodiscard]] size_t RuntimeResourceCount() const;
    [[nodiscard]] size_t LoadedResourceCount() const;
    [[nodiscard]] size_t CacheHitCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
