#include "physics/jolt/jolt_shapes.h"
#include "physics/jolt/jolt_simulation_impl.h"

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>

namespace Spot::Detail {

PhysicsCharacterId PhysicsSimulation::CreateCharacter(const PhysicsCharacterDesc& desc) {
    const float radius = std::max(desc.radius, MinShapeSize);
    const float halfHeight = std::max(desc.height * 0.5f - radius, 0.0f);
    const JPH::RefConst<JPH::Shape> shape = halfHeight > 0.0f
                                                ? JPH::RefConst<JPH::Shape>(new JPH::CapsuleShape(halfHeight, radius))
                                                : JPH::RefConst<JPH::Shape>(new JPH::SphereShape(radius));
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = shape;
    settings->mInnerBodyShape = shape;
    settings->mInnerBodyLayer = ToObjectLayer(desc.layer, true);
    settings->mMaxSlopeAngle = desc.maxSlopeAngle;
    settings->mMass = std::max(desc.mass, MinShapeSize);
    settings->mMaxStrength = std::max(desc.maxStrength, 0.0f);
    // Ground below the capsule's lower half counts as ground to stand on.
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -halfHeight);

    auto character = std::make_unique<JPH::CharacterVirtual>(settings, ToJolt(desc.position), JPH::Quat::sIdentity(),
                                                             &m_impl->system);
    std::size_t slot = 0;
    while (slot < m_impl->characters.size() && m_impl->characters[slot]) {
        ++slot;
    }
    if (slot == m_impl->characters.size()) {
        m_impl->characters.emplace_back();
        m_impl->characterLayers.push_back(0);
    }
    m_impl->characters[slot] = std::move(character);
    m_impl->characterLayers[slot] = desc.layer;
    return static_cast<PhysicsCharacterId>(slot);
}

void PhysicsSimulation::DestroyCharacter(PhysicsCharacterId character) {
    JPH::CharacterVirtual& destroyed = m_impl->GetCharacter(character);
    m_impl->contacts.ForgetBody(destroyed.GetInnerBodyID());
    m_impl->characters[static_cast<std::size_t>(character)].reset();
}

void PhysicsSimulation::SetCharacterPosition(PhysicsCharacterId character, Vector3 position) {
    m_impl->GetCharacter(character).SetPosition(ToJolt(position));
}

void PhysicsSimulation::MoveCharacter(PhysicsCharacterId id, Vector3 velocity, float deltaTime, float stepHeight,
                                      std::vector<PhysicsCharacterContact>& contacts) {
    JPH::CharacterVirtual& character = m_impl->GetCharacter(id);
    const JPH::ObjectLayer layer = ToObjectLayer(m_impl->characterLayers[static_cast<std::size_t>(id)], true);
    character.SetLinearVelocity(ToJolt(velocity));
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -std::max(stepHeight, 0.0f), 0.0f);
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(stepHeight, 0.0f), 0.0f);
    const JPH::IgnoreSingleBodyFilter ignoreOwnBody(character.GetInnerBodyID());
    if (deltaTime > 0.0f) {
        character.ExtendedUpdate(
            deltaTime, m_impl->system.GetGravity(), settings, m_impl->system.GetDefaultBroadPhaseLayerFilter(layer),
            m_impl->system.GetDefaultLayerFilter(layer), ignoreOwnBody, {}, m_impl->runtime.Get().tempAllocator);
    }

    contacts.clear();
    for (const JPH::CharacterContact& contact : character.GetActiveContacts()) {
        if (contact.mBodyB.IsInvalid() || (!contact.mHadCollision && !contact.mIsSensorB)) {
            continue;
        }
        const JPH::RefConst<JPH::Shape> shape = m_impl->GetBodies().GetShape(contact.mBodyB);
        contacts.push_back({.body = FromJolt(contact.mBodyB),
                            .shape = shape ? GetShapeIndex(*shape, contact.mSubShapeIDB) : 0,
                            .point = FromJolt(JPH::Vec3(contact.mPosition)),
                            .normal = FromJolt(contact.mContactNormal),
                            .velocity = FromJolt(contact.mLinearVelocity),
                            .sensor = contact.mIsSensorB});
    }
}

PhysicsCharacterState PhysicsSimulation::GetCharacterState(PhysicsCharacterId id) const {
    const JPH::CharacterVirtual& character = m_impl->GetCharacter(id);
    return {.position = FromJolt(JPH::Vec3(character.GetPosition())),
            .velocity = FromJolt(character.GetLinearVelocity()),
            .grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround,
            .groundNormal = FromJolt(character.GetGroundNormal()),
            .groundVelocity = FromJolt(character.GetGroundVelocity()),
            .groundBody = FromJolt(character.GetGroundBodyID())};
}

PhysicsBodyId PhysicsSimulation::GetCharacterBody(PhysicsCharacterId character) const {
    return FromJolt(m_impl->GetCharacter(character).GetInnerBodyID());
}

} // namespace Spot::Detail
