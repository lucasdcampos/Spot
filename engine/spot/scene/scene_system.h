#pragma once

#include <exception>
#include <string_view>

namespace Spot {

class Scene;
class Component;
class Entity;

// Where systems run among the others of their scene: lower orders run first, and systems of the same order run in
// the order they were added.
namespace SystemOrder {

inline constexpr int Physics = -1000;
inline constexpr int Default = 0;

} // namespace SystemOrder

// Logic that runs over a whole scene rather than per entity, such as physics, at fixed points of Scene::Update().
// The engine's systems are created when the scene first needs them; games add their own with Scene::AddSystem():
//
//     class Spawner final : public Spot::SceneSystem {
//     protected:
//         void OnUpdate(float deltaTime) override { ... }
//     };
//
//     scene.AddSystem<Spawner>();
//
// Each frame, Scene::Update() calls, for every system in order, after the components' hooks of the same point:
// - OnBeforeFixedSteps(), once components started, before the frame's fixed steps.
// - OnFixedUpdate() on each fixed step, after the components' OnFixedUpdate().
// - OnAfterFixedSteps(), after the fixed steps.
// - OnUpdate(), after the components' OnUpdate().
// - OnLateUpdate(), after the components' OnLateUpdate().
// Scene::PrepareToRender() calls OnBeforeRender() before the scene is drawn, on frames that are.
//
// A hook that throws faults its system, as it does a component: the error is logged once, and the system is skipped
// until the scene is reloaded.
class SceneSystem {
public:
    virtual ~SceneSystem() = default;

    SceneSystem(const SceneSystem&) = delete;
    SceneSystem& operator=(const SceneSystem&) = delete;

    Scene& GetScene() const { return *m_scene; }
    int GetOrder() const { return m_order; }
    bool IsFaulted() const { return m_faulted; }

protected:
    // The scene is not available in the constructor yet.
    explicit SceneSystem(int order = SystemOrder::Default) : m_order(order) {}

    // Notifications run immediately after a change, under the same fault guard as update hooks.
    virtual void OnComponentAdded(Component& /*component*/) {}
    virtual void OnComponentRemoved(Component& /*component*/) {}
    virtual void OnEntityLayerChanged(Entity& /*entity*/) {}
    virtual void OnEntityDestroyed(Entity& /*entity*/) {}
    // The source hears this before transfer, the destination after it. Both scenes stay alive during the hook.
    virtual void OnEntityMoved(Entity& /*entity*/, Scene& /*source*/, Scene& /*destination*/) {}

    virtual void OnBeforeFixedSteps(float /*deltaTime*/) {}
    virtual void OnFixedUpdate(float /*fixedDeltaTime*/) {}
    virtual void OnAfterFixedSteps(float /*deltaTime*/) {}
    virtual void OnUpdate(float /*deltaTime*/) {}
    virtual void OnLateUpdate(float /*deltaTime*/) {}
    virtual void OnBeforeRender() {}

private:
    friend class Scene;
    friend class UiInputSystem;

    // Runs one of the system's hooks, faulting the system when it throws.
    template <typename F>
    void InvokeHook(std::string_view hook, F&& fn) {
        try {
            fn();
        } catch (...) {
            Fault(hook, std::current_exception());
        }
    }
    void Fault(std::string_view hook, const std::exception_ptr& error);

    Scene* m_scene = nullptr;
    // The name of its type, which the profiler times its hooks under.
    std::string_view m_profileName;
    int m_order;
    bool m_faulted = false;
};

} // namespace Spot
