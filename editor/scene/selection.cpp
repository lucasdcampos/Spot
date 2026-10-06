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

} // namespace Spot::Editor
