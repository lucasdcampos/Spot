#include "physics/physics_simulation.h"

#include "core/log.h"

// Jolt.h comes before every other Jolt header.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace Spot::Detail {

namespace {

constexpr JPH::uint MaxBodies = 16384;
constexpr JPH::uint MaxBodyPairs = 16384;
constexpr JPH::uint MaxContactConstraints = 8192;
constexpr std::size_t TempAllocatorSize = 16 * 1024 * 1024;
// Smaller shapes make the simulation unstable.
constexpr float MinShapeSize = 0.001f;

// Jolt's object layers pair the entity's layer with whether the body moves: the layer times two, plus one for moving
// bodies.

JPH::ObjectLayer ToObjectLayer(int layer, bool moving) {
    return static_cast<JPH::ObjectLayer>(layer * 2 + (moving ? 1 : 0));
}

bool IsMoving(JPH::ObjectLayer layer) {
    return (layer & 1) != 0;
}

int GetEntityLayer(JPH::ObjectLayer layer) {
    return layer / 2;
}

using LayerMasks = std::array<std::uint32_t, 32>;

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
        return IsMoving(layer) ? BroadPhaseLayers::Moving : BroadPhaseLayers::Static;
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
        return IsMoving(layer) || broadPhaseLayer == BroadPhaseLayers::Moving;
    }
};

// Static bodies never collide with each other; other pairs collide when their entities' layers do. The masks change
// only between steps, while no thread reads them.
class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    explicit ObjectPairFilter(const LayerMasks& masks) : m_masks(masks) {}

    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (!IsMoving(a) && !IsMoving(b)) {
            return false;
        }
        return ((m_masks[static_cast<std::size_t>(GetEntityLayer(a))] >> GetEntityLayer(b)) & 1u) != 0;
    }

private:
    const LayerMasks& m_masks;
};

void TraceToLog(const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Log::GetEngineLogger().Info("Jolt: {}", message);
}

#ifdef JPH_ENABLE_ASSERTS
// Without this, a failed Jolt assert stops the program at a breakpoint without saying why.
bool AssertFailedToLog(const char* expression, const char* message, const char* file, JPH::uint line) {
    Log::GetEngineLogger().Error("Jolt assert failed at {}:{}: {}{}{}", file, line, expression, message ? ": " : "",
                                 message ? message : "");
    Log::Flush();
    return true;
}
#endif

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
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &AssertFailedToLog;)
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

JPH::Vec3 ClampLength(JPH::Vec3Arg v, float maxLength) {
    // Below the limit by a margin, which rounding cannot cross.
    const float length = v.Length();
    return length > maxLength ? v * (0.999f * maxLength / length) : v;
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

// The two bodies and shapes of a contact, as Jolt tells them apart.
struct ContactKey {
    std::uint32_t first;
    std::uint32_t firstShape;
    std::uint32_t second;
    std::uint32_t secondShape;

    friend bool operator==(const ContactKey&, const ContactKey&) = default;
};

struct ContactKeyHash {
    std::size_t operator()(const ContactKey& key) const {
        std::uint64_t hash = 1469598103934665603ull;
        for (const std::uint32_t value : {key.first, key.firstShape, key.second, key.secondShape}) {
            hash = (hash ^ value) * 1099511628211ull;
        }
        return static_cast<std::size_t>(hash);
    }
};

JPH::Vec3 GetPointVelocity(const JPH::Body& body, JPH::RVec3Arg point) {
    if (body.IsStatic()) {
        return JPH::Vec3::sZero();
    }
    return body.GetLinearVelocity() +
           body.GetAngularVelocity().Cross(JPH::Vec3(point - body.GetCenterOfMassPosition()));
}

float GetInverseMass(const JPH::Body& body) {
    return body.IsDynamic() ? body.GetMotionProperties()->GetInverseMass() : 0.0f;
}

// Keeps the contacts the bodies have, as Jolt reports them from its threads during a step. Jolt also reports contacts
// removed when a body falls asleep, which must not end them, so it decides when they end itself: after a step, a
// contact Jolt did not find again ends unless one of its bodies sleeps.
class ContactRecorder final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& /*settings*/) override {
        Record(first, second, manifold);
    }

    void OnContactPersisted(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& /*settings*/) override {
        Record(first, second, manifold);
    }

    // The contacts kept from now on were not found again.
    void BeginStep() {
        for (auto& [key, contact] : m_contacts) {
            contact.active = false;
        }
    }

    void EndStep(const JPH::BodyInterface& bodies) {
        const auto sleeping = [&bodies](std::uint32_t id) {
            const JPH::BodyID body(id);
            return bodies.GetMotionType(body) != JPH::EMotionType::Static && !bodies.IsActive(body);
        };
        std::erase_if(m_contacts, [&](const auto& entry) {
            const ContactKey& key = entry.first;
            return !entry.second.active && !sleeping(key.first) && !sleeping(key.second);
        });
    }

    void ForgetBody(JPH::BodyID body) {
        const std::uint32_t id = body.GetIndexAndSequenceNumber();
        std::erase_if(m_contacts,
                      [id](const auto& entry) { return entry.first.first == id || entry.first.second == id; });
    }

    void GetContacts(std::vector<PhysicsContact>& contacts) const {
        contacts.clear();
        contacts.reserve(m_contacts.size());
        for (const auto& [key, contact] : m_contacts) {
            contacts.push_back(contact);
        }
    }

private:
    void Record(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold) {
        PhysicsContact contact{
            .first = static_cast<PhysicsBodyId>(first.GetID().GetIndexAndSequenceNumber()),
            .second = static_cast<PhysicsBodyId>(second.GetID().GetIndexAndSequenceNumber()),
            .firstShape = manifold.mSubShapeID1.GetValue(),
            .secondShape = manifold.mSubShapeID2.GetValue(),
            .normal = FromJolt(manifold.mWorldSpaceNormal),
            .active = true,
        };
        const auto count = static_cast<std::uint32_t>(
            std::min<std::size_t>(manifold.mRelativeContactPointsOn1.size(), contact.points.size()));
        for (std::uint32_t i = 0; i < count; ++i) {
            contact.points[i] = FromJolt(JPH::Vec3(manifold.GetWorldSpaceContactPointOn1(i)));
        }
        contact.pointCount = count;
        if (count > 0) {
            const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
            const JPH::Vec3 velocity = GetPointVelocity(second, point) - GetPointVelocity(first, point);
            contact.relativeVelocity = FromJolt(velocity);
            // The second body moves along the normal to get out, so it approaches against it.
            const float approach = -velocity.Dot(manifold.mWorldSpaceNormal);
            const float inverseMass = GetInverseMass(first) + GetInverseMass(second);
            contact.impulse = approach > 0.0f && inverseMass > 0.0f ? approach / inverseMass : 0.0f;
        }

        const ContactKey key{first.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID1.GetValue(),
                             second.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID2.GetValue()};
        const std::scoped_lock lock(m_mutex);
        m_contacts.insert_or_assign(key, contact);
    }

    std::mutex m_mutex;
    std::unordered_map<ContactKey, PhysicsContact, ContactKeyHash> m_contacts;
};

// The layers a query sees.
class QueryLayerFilter final : public JPH::ObjectLayerFilter {
public:
    explicit QueryLayerFilter(std::uint32_t mask) : m_mask(mask) {}

    bool ShouldCollide(JPH::ObjectLayer layer) const override { return ((m_mask >> GetEntityLayer(layer)) & 1u) != 0; }

private:
    std::uint32_t m_mask;
};

// The bodies a query sees: sensors when asked, and never the ignored ones.
class QueryBodyFilter final : public JPH::BodyFilter {
public:
    explicit QueryBodyFilter(const PhysicsQueryFilter& filter) : m_filter(filter) {}

    bool ShouldCollide(const JPH::BodyID& body) const override {
        const auto id = static_cast<PhysicsBodyId>(body.GetIndexAndSequenceNumber());
        return id != m_filter.ignored[0] && id != m_filter.ignored[1];
    }

    bool ShouldCollideLocked(const JPH::Body& body) const override { return m_filter.sensors || !body.IsSensor(); }

private:
    const PhysicsQueryFilter& m_filter;
};

// The index of the shape of the body a sub shape belongs to: bodies of several shapes are compounds of them.
std::uint32_t GetShapeIndex(const JPH::Shape& shape, const JPH::SubShapeID& subShape) {
    if (shape.GetSubType() == JPH::EShapeSubType::StaticCompound) {
        JPH::SubShapeID remainder;
        return static_cast<const JPH::CompoundShape&>(shape).GetSubShapeIndexFromID(subShape, remainder);
    }
    return 0;
}

LayerMasks MakeAllLayers() {
    LayerMasks masks;
    masks.fill(0xffffffffu);
    return masks;
}

} // namespace

struct PhysicsSimulation::Impl {
    // Declared first, so the runtime outlives everything else.
    RuntimeUser runtime;
    BroadPhaseLayerMap broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhaseFilter;
    LayerMasks layerMasks = MakeAllLayers();
    ObjectPairFilter objectPairFilter{layerMasks};
    ContactRecorder contacts;
    JPH::PhysicsSystem system;

    Impl() {
        system.Init(MaxBodies, 0, MaxBodyPairs, MaxContactConstraints, broadPhaseLayers, objectVsBroadPhaseFilter,
                    objectPairFilter);
        system.SetContactListener(&contacts);
    }

    JPH::BodyInterface& GetBodies() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& GetBodies() const { return system.GetBodyInterface(); }

    PhysicsQueryHit MakeHit(const JPH::BodyID& body, const JPH::SubShapeID& subShape) const {
        const JPH::RefConst<JPH::Shape> shape = GetBodies().GetShape(body);
        return {.body = static_cast<PhysicsBodyId>(body.GetIndexAndSequenceNumber()),
                .shape = shape ? GetShapeIndex(*shape, subShape) : 0};
    }

    PhysicsQueryHit MakeRayHit(const JPH::RRayCast& ray, const JPH::RayCastResult& result, float maxDistance) const {
        PhysicsQueryHit hit = MakeHit(result.mBodyID, result.mSubShapeID2);
        const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
        hit.point = FromJolt(JPH::Vec3(point));
        hit.normal = FromJolt(
            GetBodies().GetTransformedShape(result.mBodyID).GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
        hit.distance = result.mFraction * maxDistance;
        return hit;
    }

    void Overlap(const JPH::Shape& shape, const JPH::RMat44& transform, const PhysicsQueryFilter& filter,
                 std::vector<PhysicsQueryHit>& hits) const {
        hits.clear();
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const QueryLayerFilter layers(filter.layerMask);
        const QueryBodyFilter bodies(filter);
        system.GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sOne(), transform, JPH::CollideShapeSettings(),
                                                  JPH::RVec3::sZero(), collector, {}, layers, bodies);
        for (const JPH::CollideShapeResult& result : collector.mHits) {
            const PhysicsQueryHit hit = MakeHit(result.mBodyID2, result.mSubShapeID2);
            const bool seen = std::ranges::any_of(hits, [&hit](const PhysicsQueryHit& other) {
                return other.body == hit.body && other.shape == hit.shape;
            });
            if (!seen) {
                hits.push_back(hit);
            }
        }
    }

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

void PhysicsSimulation::SetLayerMasks(const std::array<std::uint32_t, 32>& masks) {
    m_impl->layerMasks = masks;
}

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
                                       ToJolt(desc.motion), ToObjectLayer(desc.layer, !isStatic));
    settings.mIsSensor = desc.sensor;
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mGravityFactor = desc.gravityScale;
    settings.mLinearDamping = desc.linearDamping;
    settings.mAngularDamping = desc.angularDamping;
    // Jolt asserts that a new body starts within its speed limits, which a body at its limit can cross by rounding
    // when rebuilt.
    settings.mMotionQuality = desc.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    settings.mAllowSleeping = desc.allowSleeping;
    if (desc.motion == PhysicsMotion::Dynamic) {
        auto dofs = static_cast<std::uint8_t>(JPH::EAllowedDOFs::All);
        dofs &= desc.lockRotationX ? ~static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationX) : 0xff;
        dofs &= desc.lockRotationY ? ~static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationY) : 0xff;
        dofs &= desc.lockRotationZ ? ~static_cast<std::uint8_t>(JPH::EAllowedDOFs::RotationZ) : 0xff;
        settings.mAllowedDOFs = static_cast<JPH::EAllowedDOFs>(dofs);
    }
    settings.mLinearVelocity = ClampLength(ToJolt(desc.linearVelocity), settings.mMaxLinearVelocity);
    settings.mAngularVelocity = ClampLength(ToJolt(desc.angularVelocity), settings.mMaxAngularVelocity);
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
    m_impl->contacts.ForgetBody(id);
}

void PhysicsSimulation::Step(float deltaTime) {
    m_impl->contacts.BeginStep();
    Runtime& runtime = m_impl->runtime.Get();
    const JPH::EPhysicsUpdateError error =
        m_impl->system.Update(deltaTime, 1, &runtime.tempAllocator, &runtime.jobSystem);
    m_impl->contacts.EndStep(m_impl->GetBodies());
    if (error != JPH::EPhysicsUpdateError::None) {
        Log::GetEngineLogger().Warning("The physics step ran out of room for contacts; some collisions were missed");
    }
}

void PhysicsSimulation::GetContacts(std::vector<PhysicsContact>& contacts) const {
    m_impl->contacts.GetContacts(contacts);
}

bool PhysicsSimulation::CastRay(Vector3 origin, Vector3 direction, float maxDistance, const PhysicsQueryFilter& filter,
                                PhysicsQueryHit& hit) const {
    const JPH::RRayCast ray{ToJolt(origin), ToJolt(direction) * maxDistance};
    JPH::RayCastResult result;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    if (!m_impl->system.GetNarrowPhaseQuery().CastRay(ray, result, {}, layers, bodies)) {
        return false;
    }
    hit = m_impl->MakeRayHit(ray, result, maxDistance);
    return true;
}

void PhysicsSimulation::CastRayAll(Vector3 origin, Vector3 direction, float maxDistance,
                                   const PhysicsQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const {
    hits.clear();
    const JPH::RRayCast ray{ToJolt(origin), ToJolt(direction) * maxDistance};
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    m_impl->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), collector, {}, layers, bodies);
    collector.Sort();
    for (const JPH::RayCastResult& result : collector.mHits) {
        hits.push_back(m_impl->MakeRayHit(ray, result, maxDistance));
    }
}

bool PhysicsSimulation::CastSphere(Vector3 origin, float radius, Vector3 direction, float maxDistance,
                                   const PhysicsQueryFilter& filter, PhysicsQueryHit& hit) const {
    JPH::SphereShape sphere(std::max(radius, MinShapeSize));
    // On the stack, so Jolt's references must not free it.
    sphere.SetEmbedded();
    const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
        &sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(ToJolt(origin)), ToJolt(direction) * maxDistance);
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const QueryLayerFilter layers(filter.layerMask);
    const QueryBodyFilter bodies(filter);
    m_impl->system.GetNarrowPhaseQuery().CastShape(cast, JPH::ShapeCastSettings(), JPH::RVec3::sZero(), collector, {},
                                                   layers, bodies);
    if (!collector.HadHit()) {
        return false;
    }
    const JPH::ShapeCastResult& result = collector.mHit;
    hit = m_impl->MakeHit(result.mBodyID2, result.mSubShapeID2);
    hit.point = FromJolt(result.mContactPointOn2);
    // The penetration axis pushes the body out of the sphere; the normal faces the sphere.
    hit.normal = FromJolt(-result.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()));
    hit.distance = result.mFraction * maxDistance;
    return true;
}

void PhysicsSimulation::OverlapSphere(Vector3 center, float radius, const PhysicsQueryFilter& filter,
                                      std::vector<PhysicsQueryHit>& hits) const {
    JPH::SphereShape sphere(std::max(radius, MinShapeSize));
    // On the stack, so Jolt's references must not free it.
    sphere.SetEmbedded();
    m_impl->Overlap(sphere, JPH::RMat44::sTranslation(ToJolt(center)), filter, hits);
}

void PhysicsSimulation::OverlapBox(Vector3 center, Vector3 halfExtents, const Quaternion& rotation,
                                   const PhysicsQueryFilter& filter, std::vector<PhysicsQueryHit>& hits) const {
    const JPH::Vec3 extents = JPH::Vec3::sMax(ToJolt(halfExtents), JPH::Vec3::sReplicate(MinShapeSize));
    JPH::BoxShape box(extents, std::min(JPH::cDefaultConvexRadius, extents.ReduceMin()));
    box.SetEmbedded();
    m_impl->Overlap(box, JPH::RMat44::sRotationTranslation(ToJolt(rotation), ToJolt(center)), filter, hits);
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

void PhysicsSimulation::AddForceAtPosition(PhysicsBodyId body, Vector3 force, Vector3 position) {
    m_impl->GetBodies().AddForce(ToJolt(body), ToJolt(force), ToJolt(position));
}

void PhysicsSimulation::AddImpulseAtPosition(PhysicsBodyId body, Vector3 impulse, Vector3 position) {
    m_impl->GetBodies().AddImpulse(ToJolt(body), ToJolt(impulse), ToJolt(position));
}

void PhysicsSimulation::AddTorque(PhysicsBodyId body, Vector3 torque) {
    m_impl->GetBodies().AddTorque(ToJolt(body), ToJolt(torque));
}

void PhysicsSimulation::WakeUp(PhysicsBodyId body) {
    m_impl->GetBodies().ActivateBody(ToJolt(body));
}

void PhysicsSimulation::Sleep(PhysicsBodyId body) {
    m_impl->GetBodies().DeactivateBody(ToJolt(body));
}

} // namespace Spot::Detail
