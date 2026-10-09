#include "physics/jolt/jolt_contacts.h"

#include <Jolt/Physics/Body/Body.h>

#include <algorithm>

namespace Spot::Detail {

namespace {

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

} // namespace

std::size_t ContactRecorder::KeyHash::operator()(const Key& key) const {
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::uint32_t value : {key.first, key.firstShape, key.second, key.secondShape}) {
        hash = (hash ^ value) * 1099511628211ull;
    }
    return static_cast<std::size_t>(hash);
}

void ContactRecorder::OnContactAdded(const JPH::Body& first, const JPH::Body& second,
                                     const JPH::ContactManifold& manifold, JPH::ContactSettings& /*settings*/) {
    Record(first, second, manifold);
}

void ContactRecorder::OnContactPersisted(const JPH::Body& first, const JPH::Body& second,
                                         const JPH::ContactManifold& manifold, JPH::ContactSettings& /*settings*/) {
    Record(first, second, manifold);
}

void ContactRecorder::BeginStep() {
    for (auto& [key, contact] : m_contacts) {
        contact.active = false;
    }
}

void ContactRecorder::EndStep(const JPH::BodyInterface& bodies) {
    const auto sleeping = [&bodies](std::uint32_t id) {
        const JPH::BodyID body(id);
        return bodies.GetMotionType(body) != JPH::EMotionType::Static && !bodies.IsActive(body);
    };
    std::erase_if(m_contacts, [&](const auto& entry) {
        const Key& key = entry.first;
        return !entry.second.active && !sleeping(key.first) && !sleeping(key.second);
    });
}

void ContactRecorder::ForgetBody(JPH::BodyID body) {
    const std::uint32_t id = body.GetIndexAndSequenceNumber();
    std::erase_if(m_contacts, [id](const auto& entry) { return entry.first.first == id || entry.first.second == id; });
}

void ContactRecorder::GetContacts(std::vector<PhysicsContact>& contacts) const {
    contacts.clear();
    contacts.reserve(m_contacts.size());
    for (const auto& [key, contact] : m_contacts) {
        contacts.push_back(contact);
    }
}

void ContactRecorder::Record(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold) {
    PhysicsContact contact{
        .first = FromJolt(first.GetID()),
        .second = FromJolt(second.GetID()),
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

    const Key key{first.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID1.GetValue(),
                  second.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID2.GetValue()};
    const std::scoped_lock lock(m_mutex);
    m_contacts.insert_or_assign(key, contact);
}

} // namespace Spot::Detail
