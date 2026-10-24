#pragma once

#include "spot/core/ids/asset_id.h"
#include "spot/core/math/vector.h"
#include "spot/scene/component_type.h"
#include "spot/scene/task.h"
#include "spot/scene/timer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>

namespace Spot {

// A stable serialized component identity, scoped to its entity. It uses the same 128-bit representation as an asset
// id, but does not identify an asset in the database.
using ComponentId = AssetId;

class Archive;
class Entity;
class Scene;

namespace Detail {
struct SceneSerializer;

// Whether the component is a T, its own type or one it derives from. A cast answers it the first time a pair of types
// meets; later checks of the pair read the answer.
template <typename T>
bool IsComponentOf(const Component& component);
} // namespace Detail

// A piece of behavior attached to an entity. Custom components derive from it and override the hooks they need:
//
//     class Spinner final : public Spot::Component {
//     protected:
//         void OnUpdate(float deltaTime) override { m_angle += deltaTime; }
//     };
//
//     entity.AddComponent<Spinner>();
//
// A component is active while it is enabled, its entity is active in the hierarchy and neither is being removed.
// Its hooks run in this order:
// - OnStart() once, in the first Scene::Update() where the component is active, followed by OnEnable().
// - OnEnable() and OnDisable() each time a started component becomes active or stops being active. They run at once,
//   from the call that changed it, such as SetEnabled(), Entity::SetActive() or Entity::Destroy().
// - OnFixedUpdate() before each physics step while active, which Scene::Update() runs at a fixed rate, before
//   OnUpdate(): none, once or several times per update. Code that acts on rigidbodies, like applying forces, goes
//   there, so it behaves the same at any frame rate.
// - OnUpdate() and then OnLateUpdate() on every Scene::Update() while active. Every OnUpdate() of the scene runs
//   before the first OnLateUpdate(), so OnLateUpdate() suits work that follows others, like a camera that tracks
//   something.
// - OnDestroy() once, if OnStart() ran, when the component is removed, its entity destroyed or the scene cleared. It
//   releases what OnStart() acquired.
//
// The entity is not available in the constructor yet; setup that needs it goes in OnStart().
//
// A hook that throws faults its component: the error is logged with the entity, the component's type and the hook,
// and the component stops being active until its scene is reloaded, so the rest of the scene keeps running. It still
// gets OnDisable() if it was enabled, and OnDestroy() if it started, so it can release what it holds; see IsFaulted().
//
// To be saved and loaded with its scene, a component type is registered with RegisterComponent() and lists its fields
// with SPOT_FIELDS(), or overrides Serialize() to describe them.
class Component {
public:
    virtual ~Component();

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    Entity& GetEntity() const { return *m_entity; }
    Scene& GetScene() const;
    ComponentId GetId() const { return m_id; }

    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled);
    // Whether the component is enabled, on an entity active in the hierarchy, and neither is being removed.
    bool IsActiveAndEnabled() const;
    // Whether one of its hooks threw, which keeps it inactive for as long as it exists.
    bool IsFaulted() const { return m_faulted; }

    InvokeId Invoke(float delay, std::function<void()> callback);
    InvokeId InvokeRepeating(float delay, float interval, std::function<void()> callback);
    bool CancelInvoke(InvokeId id);
    void CancelInvokes();
    bool IsInvokePending(InvokeId id) const;
    float GetInvokeTimeLeft(InvokeId id) const;
    TaskId StartCoroutine(Task task);
    bool CancelCoroutine(TaskId id);
    void CancelCoroutines();
    bool IsCoroutineRunning(TaskId id) const;

    // Runs work on the component's behalf outside its usual hooks, such as a callback another component triggered,
    // faulting it like a hook would when the work throws. Returns whether it completed.
    bool RunGuarded(std::string_view hook, const std::function<void()>& work) { return InvokeHook(hook, work); }

protected:
    Component();

    virtual void OnStart() {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    // The scene skips these for types that keep Component's own: the first call marks the type. An
    // override has no reason to call them, and must not, or its type would be skipped.
    virtual void OnFixedUpdate(float /*fixedDeltaTime*/) {
        Detail::MarkComponentHookUnused(m_typeIndex, Detail::ComponentHook::FixedUpdate);
    }
    virtual void OnUpdate(float /*deltaTime*/) {
        Detail::MarkComponentHookUnused(m_typeIndex, Detail::ComponentHook::Update);
    }
    virtual void OnLateUpdate(float /*deltaTime*/) {
        Detail::MarkComponentHookUnused(m_typeIndex, Detail::ComponentHook::LateUpdate);
    }
    virtual void OnDestroy() {}

    // Saves or loads the component's fields. Loading happens on a new component, after it was added to its entity
    // and before OnStart(), or on an existing one when tools replace its fields.
    virtual void Serialize(Archive& /*archive*/) {}
    // Called after fields were loaded or changed by tools rather than through the component's own functions, so it
    // can bring what depends on them up to date.
    virtual void OnValidate() {}
    // Tells systems that mirror the component elsewhere, such as the render world, that it changed (see
    // Scene::TakeChangedEntities()).
    void MarkChanged();

private:
    friend class Entity;
    friend class Scene;
    friend struct Detail::SceneSerializer;
    template <typename T>
    friend bool Detail::IsComponentOf(const Component& component);

    // Calls OnEnable() or OnDisable() when the component started and its activity changed since the last call.
    void RefreshActivity();
    void UpdateInvokes(float deltaTime);
    void UpdateCoroutines(float deltaTime);

    // Runs one of the component's hooks, faulting the component when it throws. Returns whether it completed.
    template <typename F>
    bool InvokeHook(std::string_view hook, F&& fn) {
        try {
            fn();
            return true;
        } catch (...) {
            Fault(hook, std::current_exception());
            return false;
        }
    }
    void Fault(std::string_view hook, const std::exception_ptr& error);

    Entity* m_entity = nullptr;
    // Its concrete type's, set when it joins an entity.
    Detail::ComponentTypeIndex m_typeIndex = 0;
    // Its place in its scene's list of the components of its type, and when it joined it, which orders those lists
    // against each other; listed is false until it is in one.
    std::size_t m_typeSlot = 0;
    std::uint64_t m_sequence = 0;
    bool m_listed = false;
    // Its places in the scene's lists of what each stage of an update visits; see Scene::HookList.
    std::array<std::size_t, 5> m_hookSlots = {NotInList, NotInList, NotInList, NotInList, NotInList};
    static constexpr std::size_t NotInList = static_cast<std::size_t>(-1);
    ComponentId m_id = ComponentId::Generate();
    std::unique_ptr<TimerQueue> m_timers;
    std::unique_ptr<TaskScheduler> m_tasks;
    bool m_enabled = true;
    bool m_started = false;
    // Whether OnEnable() was the last of OnEnable() and OnDisable() to run.
    bool m_active = false;
    bool m_removed = false;
    bool m_faulted = false;
};

template <typename T>
bool Detail::IsComponentOf(const Component& component) {
    if constexpr (std::is_same_v<std::remove_cv_t<T>, Component>) {
        return true;
    } else {
        const ComponentTypeIndex queried = GetComponentTypeIndex<std::remove_cv_t<T>>();
        if (component.m_typeIndex == queried) {
            return true;
        }
        signed char& related = GetComponentTypeRelation(component.m_typeIndex, queried);
        if (related < 0) {
            related = dynamic_cast<const T*>(&component) != nullptr ? 1 : 0;
        }
        return related == 1;
    }
}

} // namespace Spot
