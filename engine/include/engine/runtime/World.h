#pragma once

#include "engine/asset/AssetGuid.h"
#include "engine/runtime/ComponentRegistry.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::runtime
{

using EntityId = plugin::EntityId;
inline constexpr EntityId kInvalidEntity = 0;

struct Transform
{
    glm::vec3 Position{0.0f};
    glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 Scale{1.0f};
};

struct EntityInfo
{
    EntityId Id = kInvalidEntity;
    assets::AssetGuid Guid;
    std::string Name;
    std::string Tag;
    uint32_t Layer = 0;
    EntityId Parent = kInvalidEntity;
    bool ActiveSelf = true;
    bool ActiveInHierarchy = true;
};

struct ComponentView
{
    std::string TypeName;
    uint32_t Version = 1;
    bool Enabled = true;
    void* Data = nullptr;
};

struct ConstComponentView
{
    std::string TypeName;
    uint32_t Version = 1;
    bool Enabled = true;
    const void* Data = nullptr;
};

// A compact behavior world that sits beside the renderer-neutral Scene. Scene
// remains the render data container; World supplies stable entities, hierarchy
// and plugin-defined behavior without coupling either render backend to an ECS.
class World
{
  public:
    explicit World(ComponentRegistry& components);
    ~World();

    EntityId CreateEntity(std::string name = "Entity");
    EntityId CreateEntityWithGuid(assets::AssetGuid guid,
                                  std::string name = "Entity");
    bool DestroyEntity(EntityId entity);
    bool IsAlive(EntityId entity) const;
    assets::AssetGuid GetGuid(EntityId entity) const;
    EntityId FindEntity(assets::AssetGuid guid) const;
    void Clear();

    bool SetParent(EntityId child, EntityId parent);
    EntityId GetParent(EntityId entity) const;
    std::vector<EntityId> GetChildren(EntityId entity) const;

    bool SetLocalTransform(EntityId entity, const Transform& transform);
    Transform GetLocalTransform(EntityId entity) const;
    glm::mat4 GetWorldTransform(EntityId entity);

    bool SetActive(EntityId entity, bool active);
    bool IsActive(EntityId entity);
    bool SetName(EntityId entity, std::string name);
    std::string GetName(EntityId entity) const;
    bool SetTag(EntityId entity, std::string tag);
    std::string GetTag(EntityId entity) const;
    bool SetLayer(EntityId entity, uint32_t layer);
    uint32_t GetLayer(EntityId entity) const;

    bool AddComponent(EntityId entity, const std::string& typeName,
                      std::string* error = nullptr);
    bool RemoveComponent(EntityId entity, const std::string& typeName);
    bool HasComponent(EntityId entity, const std::string& typeName) const;
    void* GetComponentData(EntityId entity, const std::string& typeName);
    const void* GetComponentData(EntityId entity, const std::string& typeName) const;
    template <typename T> T* GetComponent(EntityId entity, const std::string& typeName)
    {
        return static_cast<T*>(GetComponentData(entity, typeName));
    }
    template <typename T> const T* GetComponent(EntityId entity,
                                                const std::string& typeName) const
    {
        return static_cast<const T*>(GetComponentData(entity, typeName));
    }
    std::vector<ComponentView> ListComponents(EntityId entity);
    std::vector<ConstComponentView> ListComponents(EntityId entity) const;
    bool GetComponentProperty(EntityId entity, const std::string& typeName,
                              std::string_view property, PropertyValue& value,
                              std::string* error = nullptr) const;
    bool SetComponentProperty(EntityId entity, const std::string& typeName,
                              std::string_view property, const PropertyValue& value,
                              std::string* error = nullptr);
    bool IsComponentEnabled(EntityId entity, const std::string& typeName) const;
    bool SetComponentEnabled(EntityId entity, const std::string& typeName, bool enabled);

    void Update(float deltaSeconds);
    size_t EntityCount() const { return m_entityCount; }
    std::vector<EntityInfo> ListEntities();
    const ComponentRegistry& Components() const { return m_components; }
    ComponentRegistry& Components() { return m_components; }

  private:
    struct EntityRecord
    {
        EntityId Id = kInvalidEntity;
        assets::AssetGuid Guid;
        std::string Name;
        std::string Tag;
        uint32_t Layer = 0;
        EntityId Parent = kInvalidEntity;
        std::vector<EntityId> Children;
        Transform Local;
        glm::mat4 World{1.0f};
        std::vector<ComponentInstance> Components;
        bool ActiveSelf = true;
        bool ActiveInHierarchy = true;
        bool PendingDestroy = false;
    };

    struct Slot
    {
        uint32_t Generation = 1;
        bool Occupied = false;
        EntityRecord Record;
    };

    static EntityId MakeId(uint32_t index, uint32_t generation);
    static uint32_t IndexOf(EntityId id);
    static uint32_t GenerationOf(EntityId id);
    EntityRecord* TryGet(EntityId entity);
    const EntityRecord* TryGet(EntityId entity) const;
    bool WouldCreateCycle(EntityId child, EntityId parent) const;
    void MarkDestroyPending(EntityId entity);
    void DestroyNow(EntityId entity);
    void RefreshHierarchy();
    void RefreshNode(EntityRecord& entity, const glm::mat4& parentTransform, bool parentActive);
    void ApplyComponentActivation(EntityRecord& entity);

    ComponentRegistry& m_components;
    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_freeSlots;
    std::vector<EntityId> m_deferredDestroy;
    std::unordered_map<assets::AssetGuid, EntityId, assets::AssetGuidHash> m_guidToEntity;
    size_t m_entityCount = 0;
    bool m_hierarchyDirty = true;
    bool m_updating = false;
};

} // namespace engine::runtime
