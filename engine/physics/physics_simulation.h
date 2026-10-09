#pragma once

#include "math/quaternion.h"
#include "math/vector.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// Contract between the physics world and the physics backend chosen with SPOT_PHYSICS. Only engine sources use it and
// no backend type appears in it, so switching backends leaves the public API untouched. The engine decides which
// bodies exist and where they are placed; the backend only simulates them.
namespace Spot::Detail {

enum class PhysicsBodyId : std::uint32_t { Invalid = 0xffffffff };

enum class PhysicsMotion {
    // Never moves by itself, and only moves when placed elsewhere.
    Static,
    // Moved by the game, pushing dynamic bodies away without being pushed back.
    Kinematic,
    // Moved by gravity, forces and collisions.
    Dynamic,
};

enum class PhysicsShapeKind { Box, Sphere, Capsule };

// A shape in the space of its body, already scaled. Backends keep shapes that are too small to simulate at a minimum
// size.
struct PhysicsShape {
    PhysicsShapeKind kind = PhysicsShapeKind::Box;
    Vector3 center = {};
    // Boxes only.
    Vector3 halfExtents = {};
    // Spheres and capsules.
    float radius = 0.0f;
    // Capsules only: half the height of the cylinder between their caps, along Y.
    float halfHeight = 0.0f;
};

struct PhysicsBodyDesc {
    PhysicsMotion motion = PhysicsMotion::Static;
    Vector3 position = {};
    Quaternion rotation = {};
    // A body without shapes collides with nothing, but still moves.
    std::vector<PhysicsShape> shapes;
    // The layer of its entity, which decides what it collides with; see SetLayerMasks().
    int layer = 0;
    // Detects the bodies it overlaps without pushing them, as triggers do.
    bool sensor = false;
    float friction = 0.5f;
    float restitution = 0.0f;
    // The rest only matters to dynamic and kinematic bodies.
    float mass = 1.0f;
    float gravityScale = 1.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;
    Vector3 linearVelocity = {};
    Vector3 angularVelocity = {};
};

// Two bodies touching through one shape of each, after the last step. Contacts last from the step the shapes start
// touching until the step they stop, or until one of the bodies is destroyed, and are kept while the bodies sleep.
struct PhysicsContact {
    PhysicsBodyId first = PhysicsBodyId::Invalid;
    PhysicsBodyId second = PhysicsBodyId::Invalid;
    // Which shape of each body touches, to tell contacts between the same bodies apart.
    std::uint32_t firstShape = 0;
    std::uint32_t secondShape = 0;
    // From the first body toward the second: the way the second is pushed out.
    Vector3 normal = {};
    std::array<Vector3, 4> points = {};
    std::uint32_t pointCount = 0;
    // The second body's velocity minus the first's, at the first point.
    Vector3 relativeVelocity = {};
    // The impulse that stops the bodies moving into each other, estimated from their masses and how fast they met.
    float impulse = 0.0f;
    // Whether the contact was found or kept in the last step, rather than kept while the bodies sleep.
    bool active = false;
};

struct PhysicsPose {
    Vector3 position = {};
    Quaternion rotation = {};
};

class PhysicsSimulation {
public:
    static std::unique_ptr<PhysicsSimulation> Create();

    ~PhysicsSimulation();

    PhysicsSimulation(const PhysicsSimulation&) = delete;
    PhysicsSimulation& operator=(const PhysicsSimulation&) = delete;

    void SetGravity(Vector3 gravity);
    // For each of the 32 layers, a bit per layer its bodies collide with; the masks are symmetric. Every layer
    // collides with every layer until this is called.
    void SetLayerMasks(const std::array<std::uint32_t, 32>& masks);

    // Returns PhysicsBodyId::Invalid, after logging why, when the body cannot be created.
    PhysicsBodyId CreateBody(const PhysicsBodyDesc& desc);
    // Bodies resting on a destroyed body wake up, so they fall when it was holding them.
    void DestroyBody(PhysicsBodyId body);

    void Step(float deltaTime);
    // Replaces the vector's contents with every contact after the last step, in no particular order.
    void GetContacts(std::vector<PhysicsContact>& contacts) const;

    PhysicsPose GetPose(PhysicsBodyId body) const;
    // Places the body at once, without moving through the space in between. Bodies around both places wake up.
    void SetPose(PhysicsBodyId body, Vector3 position, const Quaternion& rotation);
    // Gives a kinematic body the velocity that brings it to the pose after deltaTime, so it pushes what is in its way.
    void MoveKinematic(PhysicsBodyId body, Vector3 position, const Quaternion& rotation, float deltaTime);
    // Whether the body moved in the last step; resting bodies fall asleep until something wakes them.
    bool IsAwake(PhysicsBodyId body) const;

    Vector3 GetLinearVelocity(PhysicsBodyId body) const;
    void SetLinearVelocity(PhysicsBodyId body, Vector3 velocity);
    Vector3 GetAngularVelocity(PhysicsBodyId body) const;
    void SetAngularVelocity(PhysicsBodyId body, Vector3 velocity);
    // Dynamic bodies only. A force acts during the next step and is then forgotten; an impulse changes the velocity
    // at once.
    void AddForce(PhysicsBodyId body, Vector3 force);
    void AddImpulse(PhysicsBodyId body, Vector3 impulse);

private:
    struct Impl;

    explicit PhysicsSimulation(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot::Detail
