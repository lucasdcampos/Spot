#pragma once

#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/physics_simulation.h"
#include "scene/entity.h"
#include "scene/scene_system.h"

#include "scene/scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Spot {

// Simulates the colliders and rigidbodies of a scene. It is the scene's system for physics, created the first time
// Scene::GetPhysics() is called, as colliders and rigidbodies do. It steps on each of the scene's fixed steps, so it
// behaves the same at any frame rate.
//
// The active colliders of an entity form one body, which is static without a Rigidbody: it stays where its entity is
// placed, like floors and walls. A Rigidbody makes the body dynamic, moved by gravity, forces and collisions, or
// kinematic, moved by its entity and pushing dynamic bodies away. Each step, bodies follow the entities that were
// moved since the last one, then the simulation runs and entities follow their dynamic bodies.
class PhysicsWorld final : public SceneSystem {
public:
    static constexpr Vector3 DefaultGravity = {0.0f, -9.81f, 0.0f};

    ~PhysicsWorld() override;

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // An acceleration, in units per second squared.
    Vector3 GetGravity() const { return m_gravity; }
    void SetGravity(Vector3 gravity);

    // The bodies simulated in the last step.
    std::size_t GetBodyCount() const { return m_bodies.size(); }

    // Which layers' bodies collide with which (see Entity::GetLayer()), as the game's physics settings say. The
    // matrix stays symmetric: setting whether a collides with b also sets whether b collides with a.
    bool GetLayerCollision(int a, int b) const;
    void SetLayerCollision(int a, int b, bool collides);
    // A bit per layer that bodies of this layer collide with.
    std::uint32_t GetLayerMask(int layer) const { return m_layerMasks[static_cast<std::size_t>(layer)]; }
    void SetLayerMask(int layer, std::uint32_t mask);

    // The layers' names, empty for unnamed ones; see PhysicsSettings::layerNames.
    const std::string& GetLayerName(int layer) const { return m_layerNames[static_cast<std::size_t>(layer)]; }
    void SetLayerNames(std::span<const std::string> names);
    // The layer with the name; nothing when no layer has it.
    std::optional<int> FindLayer(std::string_view name) const;

private:
    friend class Collider;
    friend class Entity;
    friend class Rigidbody;
    friend class Scene;

    struct Body {
        Detail::PhysicsBodyId id = Detail::PhysicsBodyId::Invalid;
        Detail::PhysicsMotion motion = Detail::PhysicsMotion::Static;
        // The world pose and scale of the entity when the body last followed it or it last followed the body, to tell
        // when something else moved it.
        Vector3 position = {};
        Quaternion rotation = {};
        Vector3 scale = {};
    };

    PhysicsWorld();

    void OnFixedUpdate(float fixedDeltaTime) override;

    // Rebuilds the entity's body in the next step, or before it when its rigidbody needs it.
    void MarkChanged(EntityId entity);
    // The entity's body when it is dynamic or kinematic, brought up to date first; null otherwise.
    const Body* FindMovingBody(EntityId entity);
    Detail::PhysicsSimulation& GetSimulation();
    void RebuildChanged();
    void Rebuild(EntityId entity);
    void FollowEntities();
    void FollowBodies();

    // Created with the first body, so scenes without physics do not pay for it.
    std::unique_ptr<Detail::PhysicsSimulation> m_simulation;
    std::unordered_map<EntityId, Body> m_bodies;
    std::unordered_set<EntityId> m_changed;
    Vector3 m_gravity = DefaultGravity;
    std::array<std::uint32_t, Entity::LayerCount> m_layerMasks;
    std::array<std::string, Entity::LayerCount> m_layerNames;
    // The seconds the current step simulates.
    float m_stepTime = Scene::DefaultFixedTimeStep;
};

} // namespace Spot
