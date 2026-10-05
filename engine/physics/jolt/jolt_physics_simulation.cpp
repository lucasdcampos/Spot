#include "physics/physics_simulation.h"

#include "core/log.h"

// Jolt.h comes before every other Jolt header.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace Spot::Detail {

namespace {

constexpr JPH::uint MaxBodies = 16384;
constexpr JPH::uint MaxBodyPairs = 16384;
constexpr JPH::uint MaxContactConstraints = 8192;
constexpr std::size_t TempAllocatorSize = 16 * 1024 * 1024;
// Smaller shapes make the simulation unstable.
constexpr float MinShapeSize = 0.001f;

namespace Layers {

constexpr JPH::ObjectLayer Static = 0;
constexpr JPH::ObjectLayer Moving = 1;

} // namespace Layers

namespace BroadPhaseLayers {

constexpr JPH::BroadPhaseLayer Static{0};
constexpr JPH::BroadPhaseLayer Moving{1};
constexpr JPH::uint Count = 2;

} // namespace BroadPhaseLayers

// Static bodies never collide with each other, so they are kept apart from moving ones, which collide with everything.
class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::Static ? BroadPhaseLayers::Static : BroadPhaseLayers::Moving;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::Static ? "Static" : "Moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override {
        return layer == Layers::Moving || broadPhaseLayer == BroadPhaseLayers::Moving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::Moving || b == Layers::Moving;
    }
};

void TraceToLog(const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Log::GetEngineLogger().Info("Jolt: {}", message);
}

// Jolt's global state, plus the threads and scratch memory every simulation shares. Simulations step one at a time,
// so they can share them.
struct Runtime {
    JPH::TempAllocatorImpl tempAllocator{TempAllocatorSize};
    JPH::JobSystemThreadPool jobSystem{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers};
};

std::mutex s_runtimeMutex;
int s_runtimeUsers = 0;
Runtime* s_runtime = nullptr;

// Keeps the runtime alive while it exists, creating it for the first simulation and destroying it after the last.
class RuntimeUser {
public:
    RuntimeUser() {
        const std::scoped_lock lock(s_runtimeMutex);
        if (s_runtimeUsers++ == 0) {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = &TraceToLog;
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
            s_runtime = new Runtime();
        }
    }

    ~RuntimeUser() {
        const std::scoped_lock lock(s_runtimeMutex);
        if (--s_runtimeUsers == 0) {
            delete s_runtime;
            s_runtime = nullptr;
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

    RuntimeUser(const RuntimeUser&) = delete;
    RuntimeUser& operator=(const RuntimeUser&) = delete;

    Runtime& Get() const { return *s_runtime; }
};

JPH::Vec3 ToJolt(Vector3 v) {
    return {v.x, v.y, v.z};
}

JPH::Quat ToJolt(const Quaternion& q) {
    return {q.x, q.y, q.z, q.w};
}

Vector3 FromJolt(JPH::Vec3Arg v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}

Quaternion FromJolt(JPH::QuatArg q) {
    return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

JPH::BodyID ToJolt(PhysicsBodyId body) {
    return JPH::BodyID(static_cast<JPH::uint32>(body));
}

JPH::EMotionType ToJolt(PhysicsMotion motion) {
    switch (motion) {
    case PhysicsMotion::Static:
        return JPH::EMotionType::Static;
    case PhysicsMotion::Kinematic:
        return JPH::EMotionType::Kinematic;
    case PhysicsMotion::Dynamic:
        return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

JPH::RefConst<JPH::Shape> CreateShape(const PhysicsShape& shape) {
    const float radius = std::max(shape.radius, MinShapeSize);
    switch (shape.kind) {
    case PhysicsShapeKind::Box: {
        const JPH::Vec3 halfExtents = JPH::Vec3::sMax(ToJolt(shape.halfExtents), JPH::Vec3::sReplicate(MinShapeSize));
        // The rounding of the edges cannot exceed the box.
        return new JPH::BoxShape(halfExtents, std::min(JPH::cDefaultConvexRadius, halfExtents.ReduceMin()));
    }
    case PhysicsShapeKind::Sphere:
        return new JPH::SphereShape(radius);
    case PhysicsShapeKind::Capsule:
        if (shape.halfHeight <= 0.0f) {
            return new JPH::SphereShape(radius);
        }
        return new JPH::CapsuleShape(shape.halfHeight, radius);
    }
    return new JPH::SphereShape(radius);
}

JPH::RefConst<JPH::Shape> CreateBodyShape(const std::vector<PhysicsShape>& shapes) {
    if (shapes.empty()) {
        return new JPH::EmptyShape();
    }
    if (shapes.size() == 1) {
        const PhysicsShape& shape = shapes.front();
        if (shape.center == Vector3{}) {
            return CreateShape(shape);
        }
        return new JPH::RotatedTranslatedShape(ToJolt(shape.center), JPH::Quat::sIdentity(), CreateShape(shape));
    }

    JPH::StaticCompoundShapeSettings compound;
    for (const PhysicsShape& shape : shapes) {
        compound.AddShape(ToJolt(shape.center), JPH::Quat::sIdentity(), CreateShape(shape));
    }
    const JPH::ShapeSettings::ShapeResult result = compound.Create();
    if (result.HasError()) {
        Log::GetEngineLogger().Error("Could not combine the shapes of a body: {}", result.GetError().c_str());
        return new JPH::EmptyShape();
    }
    return result.Get();
}

} // namespace

struct PhysicsSimulation::Impl {
    // Declared first, so the runtime outlives everything else.
    RuntimeUser runtime;
    BroadPhaseLayerMap broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhaseFilter;
    ObjectPairFilter objectPairFilter;
    JPH::PhysicsSystem system;

    Impl() {
        system.Init(MaxBodies, 0, MaxBodyPairs, MaxContactConstraints, broadPhaseLayers, objectVsBroadPhaseFilter,
                    objectPairFilter);
    }

    JPH::BodyInterface& GetBodies() { return system.GetBodyInterface(); }

    void WakeBodiesAround(JPH::BodyID body) {
        JPH::AABox bounds = GetBodies().GetTransformedShape(body).GetWorldSpaceBounds();
        // Grown a little, to reach bodies that rest on it without overlapping.
        bounds.ExpandBy(JPH::Vec3::sReplicate(0.1f));
        GetBodies().ActivateBodiesInAABox(bounds, {}, {});
    }
};

std::unique_ptr<PhysicsSimulation> PhysicsSimulation::Create() {
    return std::unique_ptr<PhysicsSimulation>(new PhysicsSimulation(std::make_unique<Impl>()));
}

PhysicsSimulation::PhysicsSimulation(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

PhysicsSimulation::~PhysicsSimulation() = default;

void PhysicsSimulation::SetGravity(Vector3 gravity) {
    m_impl->system.SetGravity(ToJolt(gravity));
    // Resting bodies would otherwise ignore the new gravity until something touches them.
    JPH::BodyIDVector bodies;
    m_impl->system.GetBodies(bodies);
    m_impl->GetBodies().ActivateBodies(bodies.data(), static_cast<int>(bodies.size()));
}

PhysicsBodyId PhysicsSimulation::CreateBody(const PhysicsBodyDesc& desc) {
    const bool isStatic = desc.motion == PhysicsMotion::Static;
    JPH::BodyCreationSettings settings(CreateBodyShape(desc.shapes), ToJolt(desc.position), ToJolt(desc.rotation),
                                       ToJolt(desc.motion), isStatic ? Layers::Static : Layers::Moving);
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mGravityFactor = desc.gravityScale;
    settings.mLinearDamping = desc.linearDamping;
    settings.mAngularDamping = desc.angularDamping;
    settings.mLinearVelocity = ToJolt(desc.linearVelocity);
    settings.mAngularVelocity = ToJolt(desc.angularVelocity);
    if (desc.motion == PhysicsMotion::Dynamic) {
        const float mass = std::max(desc.mass, MinShapeSize);
        settings.mMassPropertiesOverride.mMass = mass;
        if (desc.shapes.empty()) {
            // Without shapes there is nothing to compute the inertia from, so the body turns like a ball of size 1.
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(0.1f * mass);
        } else {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        }
    }

    JPH::Body* body = m_impl->GetBodies().CreateBody(settings);
    if (!body) {
        Log::GetEngineLogger().Error("Could not create a physics body: the limit of {} bodies is reached", MaxBodies);
        return PhysicsBodyId::Invalid;
    }
    m_impl->GetBodies().AddBody(body->GetID(), isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    return static_cast<PhysicsBodyId>(body->GetID().GetIndexAndSequenceNumber());
}

void PhysicsSimulation::DestroyBody(PhysicsBodyId body) {
    const JPH::BodyID id = ToJolt(body);
    m_impl->WakeBodiesAround(id);
    m_impl->GetBodies().RemoveBody(id);
    m_impl->GetBodies().DestroyBody(id);
}

void PhysicsSimulation::Step(float deltaTime) {
    Runtime& runtime = m_impl->runtime.Get();
    const JPH::EPhysicsUpdateError error =
        m_impl->system.Update(deltaTime, 1, &runtime.tempAllocator, &runtime.jobSystem);
    if (error != JPH::EPhysicsUpdateError::None) {
        Log::GetEngineLogger().Warning("The physics step ran out of room for contacts; some collisions were missed");
    }
}

PhysicsPose PhysicsSimulation::GetPose(PhysicsBodyId body) const {
    JPH::RVec3 position;
    JPH::Quat rotation;
    m_impl->GetBodies().GetPositionAndRotation(ToJolt(body), position, rotation);
    return {.position = FromJolt(position), .rotation = FromJolt(rotation)};
}

void PhysicsSimulation::SetPose(PhysicsBodyId body, Vector3 position, const Quaternion& rotation) {
    const JPH::BodyID id = ToJolt(body);
    m_impl->WakeBodiesAround(id);
    m_impl->GetBodies().SetPositionAndRotation(id, ToJolt(position), ToJolt(rotation), JPH::EActivation::Activate);
    m_impl->WakeBodiesAround(id);
}

void PhysicsSimulation::MoveKinematic(PhysicsBodyId body, Vector3 position, const Quaternion& rotation,
                                      float deltaTime) {
    m_impl->GetBodies().MoveKinematic(ToJolt(body), ToJolt(position), ToJolt(rotation), deltaTime);
}

bool PhysicsSimulation::IsAwake(PhysicsBodyId body) const {
    return m_impl->GetBodies().IsActive(ToJolt(body));
}

Vector3 PhysicsSimulation::GetLinearVelocity(PhysicsBodyId body) const {
    return FromJolt(m_impl->GetBodies().GetLinearVelocity(ToJolt(body)));
}

void PhysicsSimulation::SetLinearVelocity(PhysicsBodyId body, Vector3 velocity) {
    m_impl->GetBodies().SetLinearVelocity(ToJolt(body), ToJolt(velocity));
}

Vector3 PhysicsSimulation::GetAngularVelocity(PhysicsBodyId body) const {
    return FromJolt(m_impl->GetBodies().GetAngularVelocity(ToJolt(body)));
}

void PhysicsSimulation::SetAngularVelocity(PhysicsBodyId body, Vector3 velocity) {
    m_impl->GetBodies().SetAngularVelocity(ToJolt(body), ToJolt(velocity));
}

void PhysicsSimulation::AddForce(PhysicsBodyId body, Vector3 force) {
    m_impl->GetBodies().AddForce(ToJolt(body), ToJolt(force));
}

void PhysicsSimulation::AddImpulse(PhysicsBodyId body, Vector3 impulse) {
    m_impl->GetBodies().AddImpulse(ToJolt(body), ToJolt(impulse));
}

} // namespace Spot::Detail
