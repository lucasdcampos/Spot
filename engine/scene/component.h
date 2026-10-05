#pragma once

namespace Spot {

class Entity;
class Scene;

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
// - OnUpdate() and then OnLateUpdate() on every Scene::Update() while active. Every OnUpdate() of the scene runs
//   before the first OnLateUpdate(), so OnLateUpdate() suits work that follows others, like a camera that tracks
//   something.
// - OnDestroy() once, if OnStart() ran, when the component is removed, its entity destroyed or the scene cleared. It
//   releases what OnStart() acquired.
//
// The entity is not available in the constructor yet; setup that needs it goes in OnStart().
class Component {
public:
    virtual ~Component() = default;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    Entity& GetEntity() const { return *m_entity; }
    Scene& GetScene() const;

    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled);
    // Whether the component is enabled, on an entity active in the hierarchy, and neither is being removed.
    bool IsActiveAndEnabled() const;

protected:
    Component() = default;

    virtual void OnStart() {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    virtual void OnLateUpdate(float /*deltaTime*/) {}
    virtual void OnDestroy() {}

private:
    friend class Entity;

    // Calls OnEnable() or OnDisable() when the component started and its activity changed since the last call.
    void RefreshActivity();

    Entity* m_entity = nullptr;
    bool m_enabled = true;
    bool m_started = false;
    // Whether OnEnable() was the last of OnEnable() and OnDisable() to run.
    bool m_active = false;
    bool m_removed = false;
};

} // namespace Spot
