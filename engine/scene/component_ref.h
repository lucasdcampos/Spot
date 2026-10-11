#pragma once

#include "scene/entity.h"
#include "scene/scene.h"

namespace Spot {

// Identifies an entity with a component of T. Resolution is scoped to the supplied scene and never caches pointers.
// T may be forward-declared when the reference is declared; it must be complete when resolved or serialized.
template <typename T>
class ComponentRef {
public:
    ComponentRef() = default;
    explicit ComponentRef(EntityId id) : m_entityId(id) {}
    explicit ComponentRef(const T* component) { Set(component); }
    EntityId GetEntityId() const { return m_entityId; }
    void SetEntityId(EntityId id) { m_entityId = id; }
    void Set(const T* component) { m_entityId = component ? component->GetEntity().GetId() : EntityId::Invalid; }
    void Reset() { m_entityId = EntityId::Invalid; }
    T* Get(const Scene& scene) const {
        const auto* entity = scene.GetEntity(m_entityId);
        return entity ? entity->GetComponent<T>() : nullptr;
    }
    bool IsValid(const Scene& scene) const { return Get(scene) != nullptr; }
    friend bool operator==(const ComponentRef&, const ComponentRef&) = default;

private:
    EntityId m_entityId = EntityId::Invalid;
};

} // namespace Spot
