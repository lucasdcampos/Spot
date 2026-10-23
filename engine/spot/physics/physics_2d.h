#pragma once

#include "spot/core/vector.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Spot {

enum class BodyId2D : std::uint64_t { Invalid = 0 };
enum class BodyMotion2D { Static, Kinematic, Dynamic };
enum class ShapeKind2D { Box, Circle, Capsule, Polygon };

struct Shape2D {
    ShapeKind2D kind = ShapeKind2D::Box;
    Vector2 center = {};
    Vector2 size = {1.0f, 1.0f};
    float radius = 0.5f;
    float height = 2.0f;
    // Convex hull of 3 to 8 vertices, in body space before adding center.
    std::vector<Vector2> vertices = {};
    float friction = 0.5f;
    float restitution = 0.0f;
    bool trigger = false;
    friend bool operator==(const Shape2D&, const Shape2D&) = default;
};

struct BodyDesc2D {
    BodyMotion2D motion = BodyMotion2D::Static;
    Vector2 position = {};
    float angle = 0.0f;
    std::vector<Shape2D> shapes = {};
    int layer = 0;
    float mass = 1.0f;
    float gravityScale = 1.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;
    Vector2 linearVelocity = {};
    float angularVelocity = 0.0f;
    bool lockRotation = false;
    bool continuous = false;
    bool allowSleeping = true;
};

struct Pose2D {
    Vector2 position = {};
    float angle = 0.0f;
};

struct QueryFilter2D {
    std::uint32_t layerMask = 0xffffffffu;
    bool includeTriggers = false;
    BodyId2D ignore = BodyId2D::Invalid;
};

struct QueryHit2D {
    BodyId2D body = BodyId2D::Invalid;
    std::uint32_t shape = 0;
    Vector2 point = {};
    Vector2 normal = {};
    float distance = 0.0f;
};

struct Contact2D {
    BodyId2D first = BodyId2D::Invalid;
    BodyId2D second = BodyId2D::Invalid;
    bool firstTrigger = false;
    bool secondTrigger = false;
    Vector2 normal = {};
    std::array<Vector2, 2> points = {};
    std::uint32_t pointCount = 0;
    Vector2 relativeVelocity = {};
    float impulse = 0.0f;
    bool active = false;
};

// Code-only physics. The backend owns no scenes or components. Angles are radians around Z.
class Physics2D {
public:
    Physics2D();
    ~Physics2D();
    Physics2D(const Physics2D&) = delete;
    Physics2D& operator=(const Physics2D&) = delete;

    void SetGravity(Vector2 gravity);
    void SetLayerMasks(const std::array<std::uint32_t, 32>& masks);
    BodyId2D CreateBody(const BodyDesc2D& desc);
    void DestroyBody(BodyId2D body);
    void Step(float deltaTime);
    Pose2D GetPose(BodyId2D body) const;
    void SetPose(BodyId2D body, Pose2D pose);
    void MoveKinematic(BodyId2D body, Pose2D pose, float deltaTime);
    Vector2 GetLinearVelocity(BodyId2D body) const;
    void SetLinearVelocity(BodyId2D body, Vector2 velocity);
    float GetAngularVelocity(BodyId2D body) const;
    void SetAngularVelocity(BodyId2D body, float velocity);
    void AddForce(BodyId2D body, Vector2 force);
    void AddImpulse(BodyId2D body, Vector2 impulse);
    void AddForceAtPosition(BodyId2D body, Vector2 force, Vector2 position);
    void AddImpulseAtPosition(BodyId2D body, Vector2 impulse, Vector2 position);
    void AddTorque(BodyId2D body, float torque);
    bool IsAwake(BodyId2D body) const;
    void WakeUp(BodyId2D body);
    void Sleep(BodyId2D body);
    void GetContacts(std::vector<Contact2D>& contacts);
    // Output vectors replace their contents and retain capacity. Cast directions need not be normalized.
    void RaycastAll(Vector2 origin, Vector2 direction, float distance, const QueryFilter2D& filter,
                    std::vector<QueryHit2D>& hits) const;
    void CircleCast(Vector2 origin, float radius, Vector2 direction, float distance, const QueryFilter2D& filter,
                    std::vector<QueryHit2D>& hits) const;
    void OverlapCircle(Vector2 center, float radius, const QueryFilter2D& filter, std::vector<QueryHit2D>& hits) const;
    void OverlapBox(Vector2 center, Vector2 size, float angle, const QueryFilter2D& filter,
                    std::vector<QueryHit2D>& hits) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
