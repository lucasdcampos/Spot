#pragma once

#include "math/vector.h"

namespace Spot {

class Archive;
class Entity;
class Renderer2D;
class Scene;

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
//
// The entity is not available in the constructor yet; setup that needs it goes in OnStart().
//
// To be saved and loaded with its scene, a component type is registered with RegisterComponent() and lists its fields
// with SPOT_FIELDS(), or overrides Serialize() to describe them.
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
    virtual void OnFixedUpdate(float /*fixedDeltaTime*/) {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    virtual void OnLateUpdate(float /*deltaTime*/) {}
    // Draws over the view of the scene with the renderer, which draws in the view's pixels, from (0, 0) at its
    // bottom-left corner to the size at its top-right one.
    virtual void OnDrawUi(Renderer2D& /*renderer*/, Vector2 /*viewSize*/) {}
    virtual void OnDestroy() {}

    // Saves or loads the component's fields. Loading happens on a new component, after it was added to its entity
    // and before OnStart(), or on an existing one when tools replace its fields.
    virtual void Serialize(Archive& /*archive*/) {}
    // Called after fields were loaded or changed by tools rather than through the component's own functions, so it
    // can bring what depends on them up to date.
    virtual void OnValidate() {}

private:
    friend class Entity;
    friend struct Detail::SceneSerializer;

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
