#include "scene/component.h"

#include "scene/entity.h"
#include "scene/scene.h"

namespace Spot {

Scene& Component::GetScene() const {
    return m_entity->GetScene();
}

void Component::SetEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (m_entity) {
        const Detail::DeferRemovalsScope defer(m_entity->GetScene());
        RefreshActivity();
    }
}

bool Component::IsActiveAndEnabled() const {
    return m_entity && m_enabled && !m_removed && !m_entity->m_destroyed && m_entity->IsActiveInHierarchy();
}

void Component::RefreshActivity() {
    const bool active = m_started && IsActiveAndEnabled();
    if (active == m_active) {
        return;
    }
    // Set first, so hooks that change the activity again are seen by the next refresh rather than repeated.
    m_active = active;
    if (active) {
        OnEnable();
    } else {
        OnDisable();
    }
}

} // namespace Spot
