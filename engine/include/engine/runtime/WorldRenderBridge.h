#pragma once

#include "engine/runtime/RenderComponents.h"
#include "engine/runtime/World.h"

#include <functional>
#include <memory>
#include <optional>

namespace engine
{
class Camera;
class Scene;
}

namespace engine::runtime
{

struct WorldRenderResolvers
{
    std::function<std::shared_ptr<MeshData>(assets::AssetGuid)> Mesh;
    // Qualified as engine::Material: an unqualified reference here would be
    // ill-formed once the member name below hides the outer type name in
    // this class's scope (GCC/Clang diagnose it; MSVC permissively accepts
    // it, so this only surfaces on non-MSVC compilers).
    std::function<std::optional<engine::Material>(assets::AssetGuid)> Material;
    std::function<std::shared_ptr<TextureData>(assets::AssetGuid)> Texture;
    std::function<std::shared_ptr<HdrImageData>(assets::AssetGuid)> Hdri;
};

struct WorldRenderSyncResult
{
    uint32_t MeshRenderers = 0;
    uint32_t PointLights = 0;
    uint32_t SpotLights = 0;
    uint32_t AreaLights = 0;
    EntityId DirectionalLight = kInvalidEntity;
    EntityId ActiveCamera = kInvalidEntity;
    EntityId Environment = kInvalidEntity;
};

class WorldRenderBridge
{
  public:
    explicit WorldRenderBridge(WorldRenderResolvers resolvers = {})
        : m_resolvers(std::move(resolvers)) {}

    void SetResolvers(WorldRenderResolvers resolvers)
    {
        m_resolvers = std::move(resolvers);
    }

    // Rebuilds the flat renderer Scene from the active World. Scene-level
    // post/shadow/visibility settings are preserved; render instances and
    // lights are regenerated so editor transform edits are visible instantly.
    WorldRenderSyncResult Synchronize(World& world, Scene& scene,
                                      Camera* activeCamera = nullptr);

  private:
    WorldRenderResolvers m_resolvers;
};

} // namespace engine::runtime
