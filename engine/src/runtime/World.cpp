#include "engine/runtime/World.h"

#include <algorithm>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::runtime
{

World::World(ComponentRegistry& components) : m_components(components) {}

World::~World()
{
    Clear();
}

EntityId World::MakeId(uint32_t index, uint32_t generation)
{
    return (static_cast<uint64_t>(generation) << 32u) | (static_cast<uint64_t>(index) + 1u);
}

uint32_t World::IndexOf(EntityId id)
{
    const uint32_t encoded = static_cast<uint32_t>(id & 0xffffffffu);
    return encoded == 0 ? std::numeric_limits<uint32_t>::max() : encoded - 1u;
}

uint32_t World::GenerationOf(EntityId id)
{
    return static_cast<uint32_t>(id >> 32u);
}

World::EntityRecord* World::TryGet(EntityId entity)
{
    const uint32_t index = IndexOf(entity);
    if (index >= m_slots.size())
        return nullptr;
    Slot& slot = m_slots[index];
    if (!slot.Occupied || slot.Generation != GenerationOf(entity))
        return nullptr;
    return &slot.Record;
}

const World::EntityRecord* World::TryGet(EntityId entity) const
{
    const uint32_t index = IndexOf(entity);
    if (index >= m_slots.size())
        return nullptr;
    const Slot& slot = m_slots[index];
    if (!slot.Occupied || slot.Generation != GenerationOf(entity))
        return nullptr;
    return &slot.Record;
}

EntityId World::CreateEntity(std::string name)
{
    uint32_t index = 0;
    if (!m_freeSlots.empty())
    {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }

    Slot& slot = m_slots[index];
    slot.Occupied = true;
    slot.Record = {};
    slot.Record.Id = MakeId(index, slot.Generation);
    slot.Record.Name = std::move(name);
    ++m_entityCount;
    m_hierarchyDirty = true;
    return slot.Record.Id;
}

bool World::DestroyEntity(EntityId entity)
{
    EntityRecord* record = TryGet(entity);
    if (!record || record->PendingDestroy)
        return false;
    if (m_updating)
    {
        MarkDestroyPending(entity);
        m_deferredDestroy.push_back(entity);
        return true;
    }
    DestroyNow(entity);
    return true;
}

void World::MarkDestroyPending(EntityId entity)
{
    EntityRecord* record = TryGet(entity);
    if (!record || record->PendingDestroy)
        return;
    record->PendingDestroy = true;
    record->ActiveInHierarchy = false;
    ApplyComponentActivation(*record);
    for (EntityId child : record->Children)
        MarkDestroyPending(child);
}

void World::DestroyNow(EntityId entity)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
        return;

    const std::vector<EntityId> children = record->Children;
    for (EntityId child : children)
        DestroyNow(child);

    if (EntityRecord* parent = TryGet(record->Parent))
        std::erase(parent->Children, entity);

    for (ComponentInstance& component : record->Components)
        m_components.Release(component);
    record->Components.clear();

    const uint32_t index = IndexOf(entity);
    Slot& slot = m_slots[index];
    slot.Record = {};
    slot.Occupied = false;
    ++slot.Generation;
    if (slot.Generation == 0)
        slot.Generation = 1;
    m_freeSlots.push_back(index);
    --m_entityCount;
    m_hierarchyDirty = true;
}

bool World::IsAlive(EntityId entity) const
{
    return TryGet(entity) != nullptr;
}

void World::Clear()
{
    m_updating = false;
    m_deferredDestroy.clear();
    for (Slot& slot : m_slots)
    {
        if (!slot.Occupied)
            continue;
        for (ComponentInstance& component : slot.Record.Components)
            m_components.Release(component);
        slot.Record.Components.clear();
        slot.Record = {};
        slot.Occupied = false;
        ++slot.Generation;
        if (slot.Generation == 0)
            slot.Generation = 1;
    }
    m_freeSlots.clear();
    m_freeSlots.reserve(m_slots.size());
    for (uint32_t i = 0; i < m_slots.size(); ++i)
        m_freeSlots.push_back(i);
    m_entityCount = 0;
    m_hierarchyDirty = true;
}

bool World::WouldCreateCycle(EntityId child, EntityId parent) const
{
    for (EntityId current = parent; current != kInvalidEntity;)
    {
        if (current == child)
            return true;
        const EntityRecord* record = TryGet(current);
        if (!record)
            break;
        current = record->Parent;
    }
    return false;
}

bool World::SetParent(EntityId child, EntityId parent)
{
    EntityRecord* childRecord = TryGet(child);
    if (!childRecord || child == parent || (parent != kInvalidEntity && !TryGet(parent)) ||
        WouldCreateCycle(child, parent))
        return false;
    if (childRecord->Parent == parent)
        return true;
    if (EntityRecord* oldParent = TryGet(childRecord->Parent))
        std::erase(oldParent->Children, child);
    childRecord->Parent = parent;
    if (EntityRecord* newParent = TryGet(parent))
        newParent->Children.push_back(child);
    m_hierarchyDirty = true;
    return true;
}

EntityId World::GetParent(EntityId entity) const
{
    const EntityRecord* record = TryGet(entity);
    return record ? record->Parent : kInvalidEntity;
}

std::vector<EntityId> World::GetChildren(EntityId entity) const
{
    const EntityRecord* record = TryGet(entity);
    return record ? record->Children : std::vector<EntityId>{};
}

bool World::SetLocalTransform(EntityId entity, const Transform& transform)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
        return false;
    record->Local = transform;
    m_hierarchyDirty = true;
    return true;
}

Transform World::GetLocalTransform(EntityId entity) const
{
    const EntityRecord* record = TryGet(entity);
    return record ? record->Local : Transform{};
}

glm::mat4 World::GetWorldTransform(EntityId entity)
{
    RefreshHierarchy();
    const EntityRecord* record = TryGet(entity);
    return record ? record->World : glm::mat4(1.0f);
}

bool World::SetActive(EntityId entity, bool active)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
        return false;
    record->ActiveSelf = active;
    m_hierarchyDirty = true;
    return true;
}

bool World::IsActive(EntityId entity)
{
    RefreshHierarchy();
    const EntityRecord* record = TryGet(entity);
    return record && record->ActiveInHierarchy;
}

bool World::SetName(EntityId entity, std::string name)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
        return false;
    record->Name = std::move(name);
    return true;
}

std::string World::GetName(EntityId entity) const
{
    const EntityRecord* record = TryGet(entity);
    return record ? record->Name : std::string{};
}

bool World::SetTag(EntityId entity, std::string tag)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
        return false;
    record->Tag = std::move(tag);
    return true;
}

std::string World::GetTag(EntityId entity) const
{
    const EntityRecord* record = TryGet(entity);
    return record ? record->Tag : std::string{};
}

bool World::SetLayer(EntityId entity, uint32_t layer)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
        return false;
    record->Layer = layer;
    return true;
}

uint32_t World::GetLayer(EntityId entity) const
{
    const EntityRecord* record = TryGet(entity);
    return record ? record->Layer : 0;
}

bool World::AddComponent(EntityId entity, const std::string& typeName, std::string* error)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
    {
        if (error)
            *error = "Cannot add a component to an invalid entity handle";
        return false;
    }
    if (HasComponent(entity, typeName))
    {
        if (error)
            *error = "Entity already has component type '" + typeName + "'";
        return false;
    }
    ComponentInstance instance;
    if (!m_components.Create(typeName, entity, instance, error))
        return false;
    record->Components.push_back(std::move(instance));
    RefreshHierarchy();
    ApplyComponentActivation(*record);
    return true;
}

bool World::RemoveComponent(EntityId entity, const std::string& typeName)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
        return false;
    const auto it = std::find_if(record->Components.begin(), record->Components.end(),
                                 [&](const ComponentInstance& item)
                                 { return item.TypeName == typeName; });
    if (it == record->Components.end())
        return false;
    m_components.Release(*it);
    record->Components.erase(it);
    return true;
}

bool World::HasComponent(EntityId entity, const std::string& typeName) const
{
    const EntityRecord* record = TryGet(entity);
    if (!record)
        return false;
    return std::any_of(record->Components.begin(), record->Components.end(),
                       [&](const ComponentInstance& item) { return item.TypeName == typeName; });
}

void* World::GetComponentData(EntityId entity, const std::string& typeName)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
        return nullptr;
    const auto it = std::find_if(record->Components.begin(), record->Components.end(),
                                 [&](const ComponentInstance& item)
                                 { return item.TypeName == typeName; });
    return it == record->Components.end() ? nullptr : it->Data;
}

bool World::SetComponentEnabled(EntityId entity, const std::string& typeName, bool enabled)
{
    EntityRecord* record = TryGet(entity);
    if (!record)
        return false;
    const auto it = std::find_if(record->Components.begin(), record->Components.end(),
                                 [&](const ComponentInstance& item)
                                 { return item.TypeName == typeName; });
    if (it == record->Components.end())
        return false;
    it->Enabled = enabled;
    ApplyComponentActivation(*record);
    return true;
}

void World::ApplyComponentActivation(EntityRecord& entity)
{
    for (ComponentInstance& component : entity.Components)
    {
        const bool shouldBeActive = entity.ActiveInHierarchy && entity.ActiveSelf &&
                                    component.Enabled && !entity.PendingDestroy;
        if (shouldBeActive == component.Active)
            continue;
        component.Active = shouldBeActive;
        if (shouldBeActive && component.Callbacks.OnActivate)
            component.Callbacks.OnActivate(component.Data);
        else if (!shouldBeActive && component.Callbacks.OnDeactivate)
            component.Callbacks.OnDeactivate(component.Data);
        if (!shouldBeActive)
            component.Started = false;
    }
}

void World::RefreshNode(EntityRecord& entity, const glm::mat4& parentTransform, bool parentActive)
{
    const glm::mat4 local = glm::translate(glm::mat4(1.0f), entity.Local.Position) *
                            glm::mat4_cast(entity.Local.Rotation) *
                            glm::scale(glm::mat4(1.0f), entity.Local.Scale);
    entity.World = parentTransform * local;
    entity.ActiveInHierarchy = parentActive && entity.ActiveSelf && !entity.PendingDestroy;
    ApplyComponentActivation(entity);
    for (EntityId child : entity.Children)
    {
        if (EntityRecord* childRecord = TryGet(child))
            RefreshNode(*childRecord, entity.World, entity.ActiveInHierarchy);
    }
}

void World::RefreshHierarchy()
{
    if (!m_hierarchyDirty)
        return;
    for (Slot& slot : m_slots)
    {
        if (slot.Occupied && slot.Record.Parent == kInvalidEntity)
            RefreshNode(slot.Record, glm::mat4(1.0f), true);
    }
    m_hierarchyDirty = false;
}

void World::Update(float deltaSeconds)
{
    RefreshHierarchy();
    m_updating = true;
    for (Slot& slot : m_slots)
    {
        if (!slot.Occupied || slot.Record.PendingDestroy)
            continue;
        for (ComponentInstance& component : slot.Record.Components)
        {
            if (!component.Active)
                continue;
            if (!component.Started)
            {
                component.Started = true;
                if (component.Callbacks.OnStart)
                    component.Callbacks.OnStart(component.Data);
            }
            if (component.Callbacks.OnUpdate)
                component.Callbacks.OnUpdate(component.Data, deltaSeconds);
        }
    }
    m_updating = false;

    const std::vector<EntityId> pending = std::move(m_deferredDestroy);
    m_deferredDestroy.clear();
    for (EntityId entity : pending)
        DestroyNow(entity);
}

std::vector<EntityInfo> World::ListEntities()
{
    RefreshHierarchy();
    std::vector<EntityInfo> result;
    result.reserve(m_entityCount);
    for (const Slot& slot : m_slots)
    {
        if (!slot.Occupied)
            continue;
        const EntityRecord& entity = slot.Record;
        result.push_back({entity.Id, entity.Name, entity.Tag, entity.Layer, entity.Parent,
                          entity.ActiveSelf, entity.ActiveInHierarchy});
    }
    return result;
}

} // namespace engine::runtime
