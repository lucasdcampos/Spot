#include "spot/scene/component.h"

#include "spot/core/debug/diagnostics.h"
#include "spot/core/debug/log.h"
#include "spot/scene/component_registry.h"
#include "spot/scene/entity.h"
#include "spot/scene/scene.h"

#include <cstdint>
#include <string>
#include <typeinfo>

namespace Spot {

Component::Component() = default;
Component::~Component() = default;

Transform& Component::GetTransform() const {
    return GetEntity().GetTransform();
}

void Component::Destroy() {
    GetEntity().Destroy();
}

InvokeId Component::Invoke(float delay, std::function<void()> callback) {
    if (m_removed || m_faulted || (m_entity && m_entity->m_destroyed)) {
        return InvokeId::Invalid;
    }
    if (!m_timers) {
        m_timers = std::make_unique<TimerQueue>();
        if (m_entity) {
            m_entity->GetScene().ScheduleComponent(*this);
        }
    }
    return m_timers->Invoke(delay, std::move(callback));
}

InvokeId Component::InvokeRepeating(float delay, float interval, std::function<void()> callback) {
    if (m_removed || m_faulted || (m_entity && m_entity->m_destroyed)) {
        return InvokeId::Invalid;
    }
    if (!m_timers) {
        m_timers = std::make_unique<TimerQueue>();
        if (m_entity) {
            m_entity->GetScene().ScheduleComponent(*this);
        }
    }
    return m_timers->InvokeRepeating(delay, interval, std::move(callback));
}

bool Component::CancelInvoke(InvokeId id) {
    return m_timers && m_timers->Cancel(id);
}

void Component::CancelInvokes() {
    if (m_timers) {
        m_timers->CancelAll();
    }
}

bool Component::IsInvokePending(InvokeId id) const {
    return m_timers && m_timers->IsPending(id);
}

float Component::GetInvokeTimeLeft(InvokeId id) const {
    return m_timers ? m_timers->GetTimeLeft(id) : 0.0f;
}

void Component::UpdateInvokes(float deltaTime) {
    if (m_timers && m_active) {
        InvokeHook("Invoke", [&] { m_timers->Update(deltaTime, [this] { return m_active; }); });
    }
}

TaskId Component::StartCoroutine(Task task) {
    if (!m_entity || m_removed || m_faulted || m_entity->m_destroyed) {
        return TaskId::Invalid;
    }
    const Detail::DeferRemovalsScope defer(GetScene());
    if (!m_tasks) {
        m_tasks = std::make_unique<TaskScheduler>();
        GetScene().ScheduleComponent(*this);
    }
    TaskId id = TaskId::Invalid;
    InvokeHook("Coroutine", [&] { id = m_tasks->Start(std::move(task), GetScene().GetUpdateCount()); });
    return id;
}

bool Component::CancelCoroutine(TaskId id) {
    return m_tasks && m_tasks->Cancel(id);
}
void Component::CancelCoroutines() {
    if (m_tasks) {
        m_tasks->CancelAll();
    }
}
bool Component::IsCoroutineRunning(TaskId id) const {
    return m_tasks && m_tasks->IsRunning(id);
}
void Component::UpdateCoroutines(float deltaTime) {
    if (m_tasks && m_active) {
        InvokeHook("Coroutine",
                   [&] { m_tasks->Update(deltaTime, GetScene().GetUpdateCount(), [this] { return m_active; }); });
    }
}

Scene& Component::GetScene() const {
    return m_entity->GetScene();
}

void Component::MarkChanged() {
    if (m_entity) {
        m_entity->GetScene().MarkEntityChanged(*m_entity);
    }
}

void Component::SetEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (m_entity) {
        m_entity->GetScene().MarkStructureChanged();
        const Detail::DeferRemovalsScope defer(m_entity->GetScene());
        RefreshActivity();
    }
}

bool Component::IsActiveAndEnabled() const {
    return m_entity && m_enabled && !m_removed && !m_faulted && !m_entity->m_destroyed &&
           m_entity->IsActiveInHierarchy();
}

void Component::RefreshActivity() {
    if (m_removed || m_faulted || (m_entity && m_entity->m_destroyed)) {
        CancelInvokes();
        CancelCoroutines();
    }
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
        m_entity->GetScene().MarkStructureChanged();
    }
    CancelInvokes();
    CancelCoroutines();
    if (m_entity) {
        const Detail::DeferRemovalsScope defer(m_entity->GetScene());
        RefreshActivity();
    }
}

} // namespace Spot
