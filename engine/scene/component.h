#pragma once

#include "assets/asset_id.h"
#include "core/task.h"
#include "core/timer.h"
#include "math/vector.h"

#include <exception>
#include <memory>
#include <string_view>

namespace Spot {

// A stable serialized component identity, scoped to its entity. It uses the same 128-bit representation as an asset
// id, but does not identify an asset in the database.
using ComponentId = AssetId;

class Archive;
class Entity;
class Joint;
class PhysicsWorld;
class Renderer2D;
class Scene;
struct Collision;
struct TriggerContact;

namespace Detail {
struct SceneSerializer;
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
// - OnDrawUi() on every frame the scene is shown while active, after it was drawn, for what goes over it, such as a
//   HUD; see Scene::DrawUi().
// - OnDestroy() once, if OnStart() ran, when the component is removed, its entity destroyed or the scene cleared. It
//   releases what OnStart() acquired.
// - The physics events, while active, after the physics step that found them; see PhysicsWorld. OnCollisionEnter()
//   when a solid body of the entity starts touching another entity's, OnCollisionStay() on each step they keep
//   touching while either is awake, and OnCollisionExit() once they stop, or one of them goes away.
//   OnTriggerEnter() and OnTriggerExit() likewise, between a trigger and a body that overlaps it, on both entities.
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

protected:
    Component();

    virtual void OnStart() {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void OnFixedUpdate(float /*fixedDeltaTime*/) {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    virtual void OnLateUpdate(float /*deltaTime*/) {}
    // Draws over the view of the scene with the renderer, which draws in the view's pixels, from (0, 0) at its
    // bottom-left corner to the size at its top-right one.
    virtual void OnDrawUi(Renderer2D& /*renderer*/, Vector2 /*viewSize*/) {}
    virtual void OnDestroy() {}

    virtual void OnCollisionEnter(const Collision& /*collision*/) {}
    virtual void OnCollisionStay(const Collision& /*collision*/) {}
    virtual void OnCollisionExit(const Collision& /*collision*/) {}
    virtual void OnTriggerEnter(const TriggerContact& /*contact*/) {}
    virtual void OnTriggerExit(const TriggerContact& /*contact*/) {}
    // A joint of the entity broke, past its break force; see Joint.
    virtual void OnJointBreak(const Joint& /*joint*/) {}

    // Saves or loads the component's fields. Loading happens on a new component, after it was added to its entity
    // and before OnStart(), or on an existing one when tools replace its fields.
    virtual void Serialize(Archive& /*archive*/) {}
    // Called after fields were loaded or changed by tools rather than through the component's own functions, so it
    // can bring what depends on them up to date.
    virtual void OnValidate() {}

private:
    friend class Entity;
    friend class PhysicsWorld;
    friend class UiWidget;
    friend struct Detail::SceneSerializer;

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

} // namespace Spot
