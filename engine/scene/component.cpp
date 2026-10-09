#include "scene/component.h"

#include "core/diagnostics.h"
#include "core/log.h"
#include "scene/component_registry.h"
#include "scene/entity.h"
#include "scene/scene.h"

#include <cstdint>
#include <string>
#include <typeinfo>

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
    return m_entity && m_enabled && !m_removed && !m_faulted && !m_entity->m_destroyed &&
           m_entity->IsActiveInHierarchy();
}

void Component::RefreshActivity() {
    const bool active = m_started && IsActiveAndEnabled();
    if (active == m_active) {
        return;
    }
    // Set first, so hooks that change the activity again are seen by the next refresh rather than repeated.
    m_active = active;
    if (active) {
        InvokeHook("OnEnable", [this] { OnEnable(); });
    } else {
        InvokeHook("OnDisable", [this] { OnDisable(); });
    }
}

void Component::Fault(std::string_view hook, const std::exception_ptr& error) {
    std::string type = GetComponentTypeName(*this);
    if (type.empty()) {
        type = Detail::GetTypeName(typeid(*this));
    }
    const std::string where =
        m_entity ? std::format("entity '{}' ({})", m_entity->GetName(), static_cast<std::uint64_t>(m_entity->GetId()))
                 : std::string("no entity");
    if (m_faulted) {
        // Only the cleanup hooks of a faulted component run, each once at most, so this cannot repeat every frame.
        Log::GetEngineLogger().Error("{} on {} also threw in {}(): {}", type, where, hook,
                                     Detail::DescribeException(error));
        return;
    }
    Log::GetEngineLogger().Error("{} on {} threw in {}(): {}. It is disabled until the scene is reloaded.", type, where,
                                 hook, Detail::DescribeException(error));
    m_faulted = true;
    if (m_entity) {
        const Detail::DeferRemovalsScope defer(m_entity->GetScene());
        RefreshActivity();
    }
}

} // namespace Spot
