#include "scene/physics/character_controller.h"

#include "assets/asset_archive.h"
#include "core/archive.h"
#include "scene/physics/physics_world.h"
#include "scene/scene.h"

namespace Spot {

void CharacterController::SetRadius(float radius) {
    m_radius = radius;
    MarkChanged();
}

void CharacterController::SetHeight(float height) {
    m_height = height;
    MarkChanged();
}

void CharacterController::SetCenter(Vector3 center) {
    m_center = center;
    MarkChanged();
}

void CharacterController::SetSlopeLimit(float radians) {
    m_slopeLimit = radians;
    MarkChanged();
}

void CharacterController::SetStepHeight(float height) {
    m_stepHeight = height;
}

void CharacterController::SetMass(float mass) {
    m_mass = mass;
    MarkChanged();
}

void CharacterController::SetPushStrength(float strength) {
    m_pushStrength = strength;
    MarkChanged();
}

void CharacterController::Move(Vector3 velocity, float deltaTime) {
    GetScene().GetPhysics().MoveCharacter(GetEntity().GetId(), velocity, deltaTime);
}

bool CharacterController::IsGrounded() const {
    const auto state = GetScene().GetPhysics().GetCharacterState(GetEntity().GetId());
    return state && state->grounded;
}

Vector3 CharacterController::GetGroundNormal() const {
    const auto state = GetScene().GetPhysics().GetCharacterState(GetEntity().GetId());
    return state ? state->groundNormal : Vector3{};
}

Vector3 CharacterController::GetGroundVelocity() const {
    const auto state = GetScene().GetPhysics().GetCharacterState(GetEntity().GetId());
    return state ? state->groundVelocity : Vector3{};
}

EntityId CharacterController::GetGround() const {
    return GetScene().GetPhysics().GetCharacterGround(GetEntity().GetId());
}

Vector3 CharacterController::GetVelocity() const {
    const auto state = GetScene().GetPhysics().GetCharacterState(GetEntity().GetId());
    return state ? state->velocity : Vector3{};
}

void CharacterController::OnEnable() {
    MarkChanged();
}

void CharacterController::OnDisable() {
    MarkChanged();
}

void CharacterController::Serialize(Archive& archive) {
    archive.Field("radius", m_radius, {.min = 0.01f, .max = 100.0f, .speed = 0.01f});
    archive.Field("height", m_height,
                  {.min = 0.02f, .max = 100.0f, .speed = 0.01f, .tooltip = "From end to end, rounded ends included"});
    archive.Field("center", m_center, {.speed = 0.01f, .tooltip = "Where the capsule is, from the entity"});
    archive.Field("slopeLimit", m_slopeLimit,
                  {.min = 0.0f, .max = 1.5708f, .angle = true, .tooltip = "The steepest slope it stands on"});
    archive.Field("stepHeight", m_stepHeight,
                  {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "The highest step it climbs"});
    archive.Field("mass", m_mass, {.min = 0.001f, .max = 100000.0f, .speed = 0.5f, .tooltip = "In kilograms"});
    archive.Field("pushStrength", m_pushStrength,
                  {.min = 0.0f, .max = 100000.0f, .speed = 1.0f, .tooltip = "The largest force it pushes bodies with"});
}

void CharacterController::MarkChanged() {
    GetScene().GetPhysics().MarkChanged(GetEntity().GetId());
}

} // namespace Spot
