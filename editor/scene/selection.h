#pragma once

#include <spot.h>

#include <cstdint>
#include <span>
#include <vector>

namespace Spot::Editor {

// The entities selected in a scene, in the order they were picked. The last one picked is the primary one, which
// windows that show a single entity, like the inspector, follow.
class Selection {
public:
    std::span<const EntityId> GetEntities() const { return m_entities; }
    bool IsEmpty() const { return m_entities.empty(); }
    bool Contains(EntityId id) const;
    // EntityId::Invalid when nothing is selected.
    EntityId GetPrimary() const { return m_entities.empty() ? EntityId::Invalid : m_entities.back(); }

    // Selects only this entity, or nothing when it is invalid.
    void Select(EntityId id);
    void Set(std::span<const EntityId> ids);
    // Adds the entity, or makes it the primary one when it already is selected.
    void Add(EntityId id);
    void Remove(EntityId id);
    // Adds the entity when it is not selected, and removes it when it is.
    void Toggle(EntityId id);
    void Clear();
    // Drops entities the scene no longer has, such as after they were deleted.
    void RemoveMissing(const Scene& scene);

    // Changes with every change, so windows can tell when to refresh what they show.
    std::uint64_t GetVersion() const { return m_version; }

private:
    std::vector<EntityId> m_entities;
    std::uint64_t m_version = 0;
};

} // namespace Spot::Editor
