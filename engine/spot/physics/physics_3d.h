#pragma once

#include "spot/core/geometry/mesh_data.h"
#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Spot {

enum class BodyId3D : std::uint32_t { Invalid = 0xffffffff };
enum class CharacterId3D : std::uint32_t { Invalid = 0xffffffff };
enum class JointId3D : std::uint32_t { Invalid = 0xffffffff };

enum class JointKind3D {
    // Holds the bodies together as they are.
    Fixed,
    // Lets them turn around an axis through a point, within limits, maybe driven by a motor.
    Hinge,
    // Keeps two points of theirs within a range of distances, or at a distance held by a spring.
    Distance,
    // Lets them turn around a point, the axis swinging within a cone and twisting within limits, as ragdolls' bones.
    SwingTwist,
};

// A joint between two bodies, or a body and the world, described in world space as the bodies are when it is made.
// Angles and motors are those of the second body turning relative to the first.
struct JointDesc3D {
    JointKind3D kind = JointKind3D::Fixed;
    // BodyId3D::Invalid joins the second body to the world.
    BodyId3D first = BodyId3D::Invalid;
    BodyId3D second = BodyId3D::Invalid;
    // Where the bodies are joined; for distance joints, the first body's point.
    Vector3 point = {};
    // Distance joints only: the second body's point.
    Vector3 secondPoint = {};
    // The axis hinges turn around and swing-twist joints twist around, and a unit vector perpendicular to it, from
    // which hinges count their angle.
    Vector3 axis = {0.0f, 1.0f, 0.0f};
    Vector3 normal = {1.0f, 0.0f, 0.0f};
    // Hinges: angles in radians, from -pi to pi, and a motor turning at a speed in radians per second, with up to a
    // torque in newton meters.
    bool limits = false;
    float minAngle = -3.14159265f;
    float maxAngle = 3.14159265f;
    bool motor = false;
    float motorSpeed = 0.0f;
    float motorTorque = 0.0f;
    // Distance joints: the range, where a negative bound is the distance when the joint is made, and a spring that
    // holds them softly when its frequency, in hertz, is positive.
    float minDistance = -1.0f;
    float maxDistance = -1.0f;
    float springFrequency = 0.0f;
    float springDamping = 0.0f;
    // Swing-twist joints: the half angle of the cone the axis swings in, and the range of the twist, in radians.
    float swingLimit = 0.0f;
    float twistMin = 0.0f;
    float twistMax = 0.0f;
};

enum class BodyMotion3D {
    // Never moves by itself, and only moves when placed elsewhere.
    Static,
    // Moved by the game, pushing dynamic bodies away without being pushed back.
    Kinematic,
    // Moved by gravity, forces and collisions.
    Dynamic,
};

enum class ShapeKind3D {
    Box,
    Sphere,
    Capsule,
    // The mesh's triangles, which only static and kinematic bodies can have.
    Mesh,
    // The smallest convex shape around the mesh's vertices, which any body can have.
    ConvexHull,
};

// A shape in the space of its body, already scaled. Backends keep shapes that are too small to simulate at a minimum
// size.
struct Shape3D {
    ShapeKind3D kind = ShapeKind3D::Box;
    Vector3 center = {};
    // Boxes only.
    Vector3 halfExtents = {0.5f, 0.5f, 0.5f};
    // Spheres and capsules.
    float radius = 0.5f;
    // Capsules only: half the height of the cylinder between their caps, along Y.
    float halfHeight = 0.5f;
    // Meshes and convex hulls only, with the scale to apply to them. Backends build a shape once per mesh and reuse
    // it while the mesh lives.
    std::shared_ptr<const MeshData> mesh = {};
    Vector3 meshScale = {1.0f, 1.0f, 1.0f};

    bool operator==(const Shape3D&) const = default;
};

struct BodyDesc3D {
    BodyMotion3D motion = BodyMotion3D::Static;
    Vector3 position = {};
    Quaternion rotation = {};
    // A body without shapes collides with nothing, but still moves.
    std::vector<Shape3D> shapes = {};
    // The collision layer, which decides what it collides with; see SetLayerMasks().
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
    // Rotations around the world's axes the body cannot make, such as all of them for characters.
    bool lockRotationX = false;
    bool lockRotationY = false;
    bool lockRotationZ = false;
    // Sweeps the body through each step, so a fast one cannot pass through thin ones.
    bool continuous = false;
    bool allowSleeping = true;
};

// Two bodies touching through one shape of each, after the last step. Contacts last from the step the shapes start
// touching until the step they stop, or until one of the bodies is destroyed, and are kept while the bodies sleep.
struct Contact3D {
    BodyId3D first = BodyId3D::Invalid;
    BodyId3D second = BodyId3D::Invalid;
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

// Which bodies a query sees.
struct QueryFilter3D {
    // A bit per layer whose bodies it sees.
    std::uint32_t layerMask = 0xffffffffu;
    bool includeTriggers = false;
    BodyId3D ignore = BodyId3D::Invalid;
    // Additional bodies it goes through, such as all those of an entity: its solid body, its sensor and its
    // character's.
    std::array<BodyId3D, 3> ignored = {BodyId3D::Invalid, BodyId3D::Invalid, BodyId3D::Invalid};
};

struct QueryHit3D {
    BodyId3D body = BodyId3D::Invalid;
    // The index of the shape in the body's BodyDesc3D::shapes.
    std::uint32_t shape = 0;
    // Casts only.
    Vector3 point = {};
    Vector3 normal = {};
    float distance = 0.0f;
};

// A character that walks: a capsule standing along Y, moved by the game rather than by forces, which slides along
// what it hits, climbs steps and pushes dynamic bodies. It also has a kinematic body of its shape, which bodies,
// sensors and queries see.
struct CharacterDesc3D {
    // The center of the capsule.
    Vector3 position = {};
    float radius = 0.4f;
    // From end to end, the caps included.
    float height = 1.8f;
    // The steepest slope, in radians, it stands on; it slides down steeper ones.
    float maxSlopeAngle = 0.785398f;
    float mass = 70.0f;
    // The largest force it pushes dynamic bodies with, in newtons.
    float maxStrength = 100.0f;
    int layer = 0;
};

struct CharacterContact3D {
    BodyId3D body = BodyId3D::Invalid;
    std::uint32_t shape = 0;
    Vector3 point = {};
    // Pointing toward the character.
    Vector3 normal = {};
    // The velocity of the body at the point.
    Vector3 velocity = {};
    bool sensor = false;
};

struct CharacterState3D {
    Vector3 position = {};
    Vector3 velocity = {};
    // Standing on ground no steeper than its slope limit.
    bool grounded = false;
    // The feet touch ground, including a slope too steep to walk up.
    bool supported = false;
    Vector3 groundNormal = {};
    Vector3 groundVelocity = {};
    BodyId3D groundBody = BodyId3D::Invalid;
};

struct Pose3D {
    Vector3 position = {};
    Quaternion rotation = {};
};

// Code-only 3D physics. The backend owns no scenes or components.
class Physics3D {
public:
    Physics3D();
    ~Physics3D();

    Physics3D(const Physics3D&) = delete;
    Physics3D& operator=(const Physics3D&) = delete;

    void SetGravity(Vector3 gravity);
    // For each of the 32 layers, a bit per layer its bodies collide with; the masks are symmetric. Every layer
    // collides with every layer until this is called.
    void SetLayerMasks(const std::array<std::uint32_t, 32>& masks);

    // Returns BodyId3D::Invalid, after logging why, when the body cannot be created.
    BodyId3D CreateBody(const BodyDesc3D& desc);
    // Bodies resting on a destroyed body wake up, so they fall when it was holding them.
    void DestroyBody(BodyId3D body);
    // Changes what a body is made of but its shapes, motion and sensing, which only a new body changes: its material,
    // layer, mass, damping, gravity, rotation locks, sweeping and sleeping. The body keeps its pose, velocity,
    // contacts and joints.
    void UpdateBody(BodyId3D body, const BodyDesc3D& desc);

    void Step(float deltaTime);
    // Returns CharacterId3D::Invalid, after logging why, when the character cannot be created.
    CharacterId3D CreateCharacter(const CharacterDesc3D& desc);
    void DestroyCharacter(CharacterId3D character);
    // Places the character at once, as when the game teleports it.
    void SetCharacterPosition(CharacterId3D character, Vector3 position);
    // Moves the character with the velocity for the time: it slides along what it hits, climbs steps up to the step
    // height and keeps to the ground down slopes and steps as high. The contacts it had during the move replace the
    // vector's contents.
    void MoveCharacter(CharacterId3D character, Vector3 velocity, float deltaTime, float stepHeight,
                       std::vector<CharacterContact3D>& contacts);
    CharacterState3D GetCharacterState(CharacterId3D character) const;
    // The kinematic body of the character's shape.
    BodyId3D GetCharacterBody(CharacterId3D character) const;

    // Returns JointId3D::Invalid, after logging why, when the joint cannot be made.
    JointId3D CreateJoint(const JointDesc3D& desc);
    void DestroyJoint(JointId3D joint);
    // The force, in newtons, the joint used to hold its points together in the last step of that length.
    float GetJointForce(JointId3D joint, float stepTime) const;
    // Hinges only: how far they turned from where they were made, in radians.
    float GetHingeAngle(JointId3D joint) const;
    void SetHingeMotor(JointId3D joint, bool enabled, float speed, float torque);

    // Replaces the vector's contents with every contact after the last step, in no particular order.
    void GetContacts(std::vector<Contact3D>& contacts) const;

    // Queries see the bodies as they are, and never wake them. Directions need not be unit vectors. The functions
    // that fill a vector replace its contents.
    bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, const QueryFilter3D& filter,
                 QueryHit3D& hit) const;
    // Every body the ray meets, nearest first, once per shape.
    void RaycastAll(Vector3 origin, Vector3 direction, float maxDistance, const QueryFilter3D& filter,
                    std::vector<QueryHit3D>& hits) const;
    bool SphereCast(Vector3 origin, float radius, Vector3 direction, float maxDistance, const QueryFilter3D& filter,
                    QueryHit3D& hit) const;
    // The shapes overlapping the sphere or box, once each, without points or normals.
    void OverlapSphere(Vector3 center, float radius, const QueryFilter3D& filter, std::vector<QueryHit3D>& hits) const;
    void OverlapBox(Vector3 center, Vector3 halfExtents, const Quaternion& rotation, const QueryFilter3D& filter,
                    std::vector<QueryHit3D>& hits) const;

    Pose3D GetPose(BodyId3D body) const;
    // Places the body at once, without moving through the space in between. Bodies around both places wake up.
    void SetPose(BodyId3D body, Pose3D pose);
    void SetPose(BodyId3D body, Vector3 position, const Quaternion& rotation) { SetPose(body, {position, rotation}); }
    // Gives a kinematic body the velocity that brings it to the pose after deltaTime, so it pushes what is in its way.
    void MoveKinematic(BodyId3D body, Pose3D pose, float deltaTime);
    void MoveKinematic(BodyId3D body, Vector3 position, const Quaternion& rotation, float deltaTime) {
        MoveKinematic(body, {position, rotation}, deltaTime);
    }
    // Whether the body moved in the last step; resting bodies fall asleep until something wakes them.
    bool IsAwake(BodyId3D body) const;

    Vector3 GetLinearVelocity(BodyId3D body) const;
    void SetLinearVelocity(BodyId3D body, Vector3 velocity);
    Vector3 GetAngularVelocity(BodyId3D body) const;
    void SetAngularVelocity(BodyId3D body, Vector3 velocity);
    // Dynamic bodies only. A force acts during the next step and is then forgotten; an impulse changes the velocity
    // at once.
    void AddForce(BodyId3D body, Vector3 force);
    void AddImpulse(BodyId3D body, Vector3 impulse);
    // At a point in world space, which also turns the body unless the point is its center of mass.
    void AddForceAtPosition(BodyId3D body, Vector3 force, Vector3 position);
    void AddImpulseAtPosition(BodyId3D body, Vector3 impulse, Vector3 position);
    void AddTorque(BodyId3D body, Vector3 torque);
    void WakeUp(BodyId3D body);
    void Sleep(BodyId3D body);

private:
    struct Impl;

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
