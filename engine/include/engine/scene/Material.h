#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

#include "engine/scene/Texture.h"

namespace engine {

// Backend-independent metallic/roughness material. Both the legacy combined
// MRAO map and standard glTF texture slots are supported.
struct Material
{
    enum class AlphaMode { Opaque, Mask };

    glm::vec3 Albedo{0.8f, 0.8f, 0.8f};
    float BaseColorAlpha = 1.0f;
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    glm::vec3 Emissive{0.0f};
    float AmbientOcclusion = 1.0f;
    float SpecularF0 = 0.04f;

    std::shared_ptr<TextureData> AlbedoMap;
    std::shared_ptr<TextureData> NormalMap;
    // glTF convention: G=roughness, B=metalness.
    std::shared_ptr<TextureData> MetallicRoughnessMap;
    std::shared_ptr<TextureData> OcclusionMap;
    std::shared_ptr<TextureData> EmissiveMap;
    // Legacy combined convention: R=metalness, G=roughness, B=AO.
    std::shared_ptr<TextureData> MraoMap;

    AlphaMode Alpha = AlphaMode::Opaque;
    float AlphaCutoff = 0.5f;
};

// Exact render-state identity shared by geometry batching, GPU instancing and
// offline asset combining. Pointer identity is intentional for texture slots:
// GPU resource caches use the same shared CPU texture objects.
uint64_t MaterialRenderStateHash(const Material& material) noexcept;
bool MaterialRenderStatesEqual(const Material& left,
                               const Material& right) noexcept;

} // namespace engine
