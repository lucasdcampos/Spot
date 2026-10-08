#pragma once

#include <spot.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace Spot::Editor {

// A place in a scene's hierarchy: the sibling indices of an entity and its ancestors, from the root down. Entities get
// new ids when a scene is loaded again or copied, but keep their places.
using HierarchyPath = std::vector<std::size_t>;

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
    // The same entities, to tell quickly whether one is selected, as windows ask for every entity they show.
    std::unordered_set<EntityId> m_set;
    std::uint64_t m_version = 0;
};

// The places of the selected entities, in the order they were picked.
std::vector<HierarchyPath> GetSelectedPaths(const Scene& scene, const Selection& selection);
// Selects the entities at the places, in order, skipping places the scene does not have.
void SelectPaths(const Scene& scene, Selection& selection, const std::vector<HierarchyPath>& paths);

} // namespace Spot::Editor
