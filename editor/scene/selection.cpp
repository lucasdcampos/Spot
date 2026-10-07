#include "scene/selection.h"

#include <algorithm>

namespace Spot::Editor {

bool Selection::Contains(EntityId id) const {
    return std::ranges::find(m_entities, id) != m_entities.end();
}

void Selection::Select(EntityId id) {
    m_entities.clear();
    if (id != EntityId::Invalid) {
        m_entities.push_back(id);
    }
    ++m_version;
}

void Selection::Set(std::span<const EntityId> ids) {
    m_entities.clear();
    for (const EntityId id : ids) {
        if (id != EntityId::Invalid && !Contains(id)) {
            m_entities.push_back(id);
        }
    }
    ++m_version;
}

void Selection::Add(EntityId id) {
    if (id == EntityId::Invalid) {
        return;
    }
    std::erase(m_entities, id);
    m_entities.push_back(id);
    ++m_version;
}

void Selection::Remove(EntityId id) {
    if (std::erase(m_entities, id) > 0) {
        ++m_version;
    }
}

void Selection::Toggle(EntityId id) {
    if (Contains(id)) {
        Remove(id);
    } else {
        Add(id);
    }
}

void Selection::Clear() {
    if (!m_entities.empty()) {
        m_entities.clear();
        ++m_version;
    }
}

void Selection::RemoveMissing(const Scene& scene) {
    if (std::erase_if(m_entities, [&](EntityId id) { return !scene.GetEntity(id); }) > 0) {
        ++m_version;
    }
}

std::vector<HierarchyPath> GetSelectedPaths(const Scene& scene, const Selection& selection) {
    std::vector<HierarchyPath> paths;
    for (const EntityId id : selection.GetEntities()) {
        const Entity* entity = scene.GetEntity(id);
        if (!entity) {
            continue;
        }
        HierarchyPath path;
        for (; entity; entity = entity->GetParent()) {
            path.insert(path.begin(), entity->GetSiblingIndex());
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

void SelectPaths(const Scene& scene, Selection& selection, const std::vector<HierarchyPath>& paths) {
    std::vector<EntityId> ids;
    for (const HierarchyPath& path : paths) {
        std::vector<Entity*> siblings = scene.GetRootEntities();
        Entity* entity = nullptr;
        for (const std::size_t index : path) {
            if (index >= siblings.size()) {
                entity = nullptr;
                break;
            }
            entity = siblings[index];
            siblings = entity->GetChildren();
        }
        if (entity) {
            ids.push_back(entity->GetId());
        }
    }
    selection.Set(ids);
}

} // namespace Spot::Editor
