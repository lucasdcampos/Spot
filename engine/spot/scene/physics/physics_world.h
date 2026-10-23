#pragma once

#include "spot/math/quaternion.h"
#include "spot/math/vector.h"
#include "spot/physics/physics_simulation.h"
#include "spot/scene/entity.h"
#include "spot/scene/physics/collision.h"
#include "spot/scene/physics/physics_debug.h"
#include "spot/scene/physics/physics_query.h"
#include "spot/scene/scene_system.h"

#include "spot/scene/scene.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Spot {

// Simulates the colliders and rigidbodies of a scene. It is the scene's system for physics, created the first time
// Scene::GetPhysics() is called, as colliders and rigidbodies do. It steps on each of the scene's fixed steps, so it
// behaves the same at any frame rate.
//
// The active colliders of an entity form one body, which is static without a Rigidbody: it stays where its entity is
// placed, like floors and walls. A Rigidbody makes the body dynamic, moved by gravity, forces and collisions, or
// kinematic, moved by its entity and pushing dynamic bodies away. Each step, bodies follow the entities that were
// moved since the last one, then the simulation runs and entities follow their dynamic bodies.
//
// After each step, the contacts between bodies become events for the components of both entities, per pair of
// entities however many shapes touch: OnCollisionEnter(), OnCollisionStay() and OnCollisionExit() between solid
// bodies, and OnTriggerEnter() and OnTriggerExit() between a trigger and another body. Bodies of the same entity, and
// two triggers, report nothing. Components may destroy entities and change colliders from these hooks; what changes
// shows from the next step.
class PhysicsWorld final : public SceneSystem {
public:
    static constexpr Vector3 DefaultGravity = {0.0f, -9.81f, 0.0f};

    ~PhysicsWorld() override;

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // An acceleration, in units per second squared.
    Vector3 GetGravity() const { return m_gravity; }
    void SetGravity(Vector3 gravity);

    // The bodies simulated in the last step, the sensor bodies of triggers included.
    std::size_t GetBodyCount() const;

    // Which layers' bodies collide with which (see Entity::GetLayer()), as the game's physics settings say. The
    // matrix stays symmetric: setting whether a collides with b also sets whether b collides with a.
    bool GetLayerCollision(int a, int b) const;
    void SetLayerCollision(int a, int b, bool collides);
    // A bit per layer that bodies of this layer collide with.
    std::uint32_t GetLayerMask(int layer) const { return m_layerMasks[static_cast<std::size_t>(layer)]; }
    void SetLayerMask(int layer, std::uint32_t mask);

    // Queries see the bodies of the last step, rebuilt first where their colliders or rigidbodies changed since; an
    // entity moved since then is seen where it was. Directions need not be unit vectors. They never wake bodies.
    //
    //     if (auto hit = physics.Raycast(eye, forward, 100.0f, {.ignore = player.GetId()})) {
    //         Spot::DebugDraw::Cross(hit->point, 0.2f);
    //     }

    // The first body the ray meets within the distance.
    std::optional<RaycastHit> Raycast(Vector3 origin, Vector3 direction, float maxDistance,
                                      const QueryFilter& filter = {});
    // Every collider the ray meets within the distance, nearest first.
    std::vector<RaycastHit> RaycastAll(Vector3 origin, Vector3 direction, float maxDistance,
                                       const QueryFilter& filter = {});
    // The first body a sphere moving along the direction meets, as for a thick ray. Bodies the sphere overlaps where it
    // starts are met at once, at distance 0.
    std::optional<RaycastHit> SphereCast(Vector3 origin, float radius, Vector3 direction, float maxDistance,
                                         const QueryFilter& filter = {});
    // The colliders overlapping the sphere, or the box of the size centered on the point, once each.
    std::vector<OverlapHit> OverlapSphere(Vector3 center, float radius, const QueryFilter& filter = {});
    std::vector<OverlapHit> OverlapBox(Vector3 center, Vector3 size, const Quaternion& rotation = {},
                                       const QueryFilter& filter = {});

    // What the world draws with DebugDraw before each frame is drawn, to see what physics does; nothing by default.
    PhysicsDebugFlags GetDebugDraw() const { return m_debugFlags; }
    void SetDebugDraw(PhysicsDebugFlags flags);

    // The layers' names, empty for unnamed ones; see PhysicsSettings::layerNames.
    const std::string& GetLayerName(int layer) const { return m_layerNames[static_cast<std::size_t>(layer)]; }
    void SetLayerNames(std::span<const std::string> names);
    // The layer with the name; nothing when no layer has it.
    std::optional<int> FindLayer(std::string_view name) const;

private:
    friend class CharacterController;
    friend class Collider;
    friend class Entity;
    friend class HingeJoint;
    friend class Joint;
    friend class Rigidbody;
    friend class Scene;

    // An entity's bodies: the one of its solid colliders and rigidbody, and the one of its triggers, either of which
    // may be missing.
    struct Body {
        Detail::PhysicsBodyId id = Detail::PhysicsBodyId::Invalid;
        Detail::PhysicsBodyId sensor = Detail::PhysicsBodyId::Invalid;
        Detail::PhysicsMotion motion = Detail::PhysicsMotion::Static;
        // The entity's CharacterController, if any: its character, where the capsule is from the entity, its mass,
        // and the contacts of its last move, which count as active until the next step.
        Detail::PhysicsCharacterId character = Detail::PhysicsCharacterId::Invalid;
        Vector3 characterCenter = {};
        float characterMass = 0.0f;
        std::vector<Detail::PhysicsCharacterContact> characterContacts = {};
        bool characterMoved = false;
        // The world pose and scale of the entity when the body last followed it or it last followed the body, to tell
        // when something else moved it.
        Vector3 position = {};
        Quaternion rotation = {};
        Vector3 scale = {};
        // What the bodies are made of, which only a new body changes.
        std::vector<Detail::PhysicsShape> shapes = {};
        std::vector<Detail::PhysicsShape> sensorShapes = {};
    };

    PhysicsWorld();

    void OnFixedUpdate(float fixedDeltaTime) override;
    void OnBeforeRender() override;

    // Rebuilds the entity's body in the next step, or before it when its rigidbody needs it.
    void MarkChanged(EntityId entity);
    // The entity's solid body when it is dynamic or kinematic, brought up to date first; null otherwise.
    const Body* FindMovingBody(EntityId entity);
    Detail::PhysicsSimulation& GetSimulation();
    void RebuildChanged();
    void Rebuild(EntityId entity);
    void FollowEntities();
    void FollowBodies();
    // See CharacterController.
    void MoveCharacter(EntityId entity, Vector3 velocity, float deltaTime);
    std::optional<Detail::PhysicsCharacterState> GetCharacterState(EntityId entity);
    EntityId GetCharacterGround(EntityId entity);
    // The entity's body record with a character, brought up to date first; null when it has none.
    Body* FindCharacter(EntityId entity);
    // Adds the contacts of the characters' last moves to the pairs of this step.
    void AddCharacterContacts();

    // See Joint. Joints wait in a list until both their bodies exist, and go back to it whenever either body is
    // rebuilt, since a joint cannot outlive its bodies.
    void MarkJointChanged(Joint& joint);
    void ForgetJoint(Joint& joint);
    Detail::PhysicsJointId FindJoint(const Joint& joint) const;
    float GetJointForce(const Joint& joint) const;
    float GetHingeAngle(const Joint& joint) const;
    void SetHingeMotor(const Joint& joint, bool enabled, float speed, float torque);
    // Takes down the joints of the entity, as one of their bodies, and puts them back in the list.
    void ReleaseJoints(EntityId entity);
    void CreatePendingJoints();
    void BreakJoints();

    // Turns the contacts after a step into events, against those of the step before.
    void DispatchContacts();
    void ForgetBody(Detail::PhysicsBodyId body);

    // Created with the first body, so scenes without physics do not pay for it.
    std::unique_ptr<Detail::PhysicsSimulation> m_simulation;
    std::unordered_map<EntityId, Body> m_bodies;
    // Which entity each body is for, whether it is the entity's sensor, and the colliders its shapes come from, in
    // order.
    struct Owner {
        EntityId entity;
        bool sensor;
        std::vector<Collider*> colliders;
    };
    Detail::PhysicsQueryFilter MakeQueryFilter(const QueryFilter& filter) const;
    // Brings the bodies up to date for a query; false when there are none.
    bool PrepareQuery();
    RaycastHit MakeHit(const Detail::PhysicsQueryHit& hit) const;
    std::vector<OverlapHit> MakeOverlapHits() const;
    std::vector<Detail::PhysicsQueryHit> m_queryHits;
    // The queries asked since the last frame was drawn, kept for the debug view while it shows them.
    struct DebugQuery {
        enum class Kind { Ray, Sphere, Box } kind = Kind::Ray;
        Vector3 origin = {};
        Vector3 end = {};
        Vector3 size = {};
        Quaternion rotation = {};
        float radius = 0.0f;
        std::optional<RaycastHit> hit = std::nullopt;
    };
    void RecordQuery(DebugQuery query);
    PhysicsDebugFlags m_debugFlags = PhysicsDebugFlags::None;
    std::vector<DebugQuery> m_debugQueries;
    struct JointRecord {
        Detail::PhysicsJointId id;
        EntityId owner;
        EntityId connected;
    };
    // Joints are referred to by pointer here because they leave the moment they are disabled, before they are freed.
    std::unordered_map<const Joint*, JointRecord> m_joints;
    std::vector<Joint*> m_pendingJoints;
    std::unordered_map<Detail::PhysicsBodyId, Owner> m_owners;
    // A pair of entities touching, the lower id first.
    struct ContactPair {
        EntityId first;
        EntityId second;
        bool trigger;
        // Whose trigger it is, for trigger pairs: the first entity's or the second's.
        bool firstIsTrigger;

        friend auto operator<=>(const ContactPair&, const ContactPair&) = default;
    };
    // A pair touching, with its contact as the first entity sees it.
    struct PairContact {
        ContactPair pair;
        Collision collision;
        bool active;
    };
    enum class ContactPhase { Enter, Stay, Exit };
    void SendContact(const PairContact& contact, ContactPhase phase);

    // The pairs touching after the last step, sorted, and the list the next step fills.
    std::vector<PairContact> m_previous;
    std::vector<PairContact> m_current;
    std::vector<Detail::PhysicsContact> m_contactBuffer;
    std::unordered_set<EntityId> m_changed;
    Vector3 m_gravity = DefaultGravity;
    std::array<std::uint32_t, Entity::LayerCount> m_layerMasks;
    std::array<std::string, Entity::LayerCount> m_layerNames;
    // The seconds the current step simulates.
    float m_stepTime = Scene::DefaultFixedTimeStep;
};

} // namespace Spot
