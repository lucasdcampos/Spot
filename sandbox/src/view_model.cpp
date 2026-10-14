#include "view_model.h"

#include "physics_gun.h"
#include "player_controller.h"
#include "player_movement.h"
#include "prop_carrier.h"
#include "weapon.h"
#include "weapon_holder.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(ViewModel);

namespace {

using Spot::Matrix4;
using Spot::Quaternion;
using Spot::Vector3;

constexpr Vector3 Right = {1.0f, 0.0f, 0.0f};
constexpr Vector3 Up = {0.0f, 1.0f, 0.0f};
constexpr Vector3 Back = {0.0f, 0.0f, 1.0f};

float SmoothStep(float from, float to, float value) {
    const float t = std::clamp((value - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// A bump of height 1 at the center, as wide as the width.
float Pulse(float value, float center, float width) {
    const float x = (value - center) / width;
    return std::exp(-x * x);
}

// Turns about the view's axes, by small angles in radians: pitch raises the muzzle, yaw swings it left and roll tilts
// the top left.
Quaternion Turn(float pitch, float yaw, float roll) {
    return Quaternion::FromAxisAngle(Up, yaw) * Quaternion::FromAxisAngle(Right, pitch) *
           Quaternion::FromAxisAngle(Back, roll);
}

Vector3 TransformPoint(const Matrix4& matrix, Vector3 point) {
    const Spot::Vector4 result = matrix * Spot::Vector4{point.x, point.y, point.z, 1.0f};
    return {result.x, result.y, result.z};
}

bool StartsWith(std::string_view name, std::string_view prefix) {
    return name.substr(0, prefix.size()) == prefix;
}

} // namespace

Spot::AssetRef<Spot::Model> ViewModel::GetHeldModel() const {
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    if (!holder) {
        return {};
    }
    if (holder->IsPhysicsGunSelected()) {
        return physicsGun;
    }
    if (holder->IsToolGunSelected()) {
        return toolGun;
    }
    const Weapon* weapon = holder->GetWeapon();
    return weapon ? weapon->model : Spot::AssetRef<Spot::Model>();
}

void ViewModel::Show(const Spot::AssetRef<Spot::Model>& model) {
    Clear();
    m_shown = model;
    if (!model) {
        return;
    }
    m_muzzle = {0.0f, 0.03f, -0.3f};
    m_hasEye = false;
    m_hip = {};
    const auto nodes = model->GetNodes();
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Matrix4 transform = model->GetNodeTransform(i);
        const Vector3 position = TransformPoint(transform, {});
        if (nodes[i].name == "Muzzle") {
            m_muzzle = position;
        } else if (nodes[i].name == "Hip") {
            m_hip = position;
        } else if (nodes[i].name == "Eye") {
            m_eye = position;
            m_hasEye = true;
        }
    }
    for (const Spot::ModelPart& part : model->GetParts()) {
        Spot::Entity& entity = GetScene().CreateEntity("View Model");
        entity.SetLayer(GetEntity().GetLayer());
        // So close to the eyes, its shadow would darken the view.
        auto& renderer = entity.AddComponent<Spot::MeshRenderer>(part.mesh, part.material);
        renderer.SetCastingShadows(false);
        renderer.SetSubmesh(part.submesh);
        m_parts.push_back(entity.GetId());
        m_partInfo.push_back({entity.GetId(), part.name, part.transform});
    }
}

void ViewModel::Clear() {
    for (const Spot::EntityId id : m_parts) {
        if (Spot::Entity* entity = GetScene().GetEntity(id)) {
            entity->Destroy();
        }
    }
    m_parts.clear();
    m_partInfo.clear();
    m_shown = {};
}

void ViewModel::Kick(float strength) {
    m_sinceShot = 0.0f;
    // Alternating, so a burst wanders a little either way rather than always the same.
    m_kickSide = -m_kickSide;
    auto& tweens = Spot::TweenSystem::Get(GetScene());
    tweens.Cancel(m_recoilTween);
    const float peak = std::clamp(m_recoil + strength, 0.0f, 0.2f);
    const auto set = [this](float value) { m_recoil = value; };
    m_recoilTween =
        tweens.Play(*this, Spot::Tween::Sequence({Spot::Tween::Value(0.03f, m_recoil, peak, set, Spot::Ease::QuadOut),
                                                  Spot::Tween::Value(0.18f, peak, 0.0f, set, Spot::Ease::SineInOut)}));
}

Spot::Vector3 ViewModel::GetMuzzlePosition() const {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!player) {
        return GetEntity().GetTransform().GetWorldPosition();
    }
    if (!m_shown) {
        return player->GetEyePosition();
    }
    return player->GetEyePosition() + player->GetViewRotation() * TransformPoint(m_pose, m_muzzle);
}

void ViewModel::OnUpdate(float /*deltaTime*/) {
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    const float target = holder && holder->IsAiming() ? 1.0f : 0.0f;
    if (target != m_aimTarget) {
        m_aimTarget = target;
        auto& tweens = Spot::TweenSystem::Get(GetScene());
        tweens.Cancel(m_aimTween);
        m_aimTween = tweens.Play(
            *this,
            Spot::Tween::Value(0.18f, m_aim, target, [this](float value) { m_aim = value; }, Spot::Ease::QuadOut));
    }
}

void ViewModel::UpdateMotion(float deltaTime, const Quaternion& view) {
    Vector3 target;
    Spot::Vector2 rates;
    float roll = 0.0f;
    if (m_hasPreviousView && Spot::Input::IsCursorLocked()) {
        auto change = Spot::Inverse(m_previousView) * view;
        if (change.w < 0) {
            change = {-change.x, -change.y, -change.z, -change.w};
        }
        // Radians per second the view turns, left and up.
        rates = {2.0f * change.y / deltaTime, 2.0f * change.x / deltaTime};
        target.x = std::clamp(rates.x * swayAmount, -maxSway, maxSway);
        target.y = std::clamp(-rates.y * swayAmount, -maxSway, maxSway);
    }
    const auto* movement = GetEntity().GetComponent<PlayerMovement>();
    if (movement && !movement->IsFlying()) {
        // A figure of eight over two steps, lowest between them, as the arms carry the weight.
        const float phase = movement->GetStepPhase() * Spot::Pi;
        const float weight = movement->GetBobWeight();
        target += Vector3{std::sin(phase), 0.5f * std::cos(2.0f * phase), 0.0f} * (bobAmount * weight);
        roll = 0.02f * weight * std::sin(phase);
    }
    m_previousView = view;
    m_hasPreviousView = true;
    const float steady = 1.0f - m_aim * 0.85f;
    const float blend = 1.0f - std::exp(-motionResponse * deltaTime);
    m_motion += (target * steady - m_motion) * blend;
    m_lag += (rates * steady - m_lag) * blend;
    m_roll += (roll * steady - m_roll) * blend;
}

void ViewModel::UpdateObstruction(float deltaTime) {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    float target = 0.0f;
    // What the player carries or grabs is in front of it on purpose.
    if (player && m_shown && !(carrier && carrier->IsHolding()) && !(gun && gun->IsGrabbing())) {
        const float reach = m_hip.z - offset.z - m_muzzle.z + 0.05f;
        const auto hit = GetScene().GetPhysics().Raycast(player->GetEyePosition(), player->GetAimDirection(), reach,
                                                         {.ignore = GetEntity().GetId()});
        if (hit) {
            target = std::clamp((reach - hit->distance) / (reach * 0.6f), 0.0f, 1.0f);
        }
    }
    m_blocked += (target - m_blocked) * (1.0f - std::exp(-10.0f * deltaTime));
}

Spot::Matrix4 ViewModel::ComputePose() const {
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    const Weapon* weapon = holder ? holder->GetWeapon() : nullptr;

    // From the hip, turned a little toward the crosshair, to the sights, in line with the eye.
    const Vector3 aimed = m_hasEye ? -m_eye : Vector3{0.0f, -0.08f, -0.3f};
    const Vector3 hip = offset - m_hip;
    Vector3 position = hip + (aimed - hip) * m_aim;
    Quaternion rotation = Spot::Slerp(Turn(0.0f, 0.045f, 0.0f), Quaternion{}, m_aim);

    // Breathing, slower and smaller down the sights.
    const float breath = 1.0f - m_aim * 0.7f;
    position += Vector3{0.0012f * std::sin(m_time * 0.9f), 0.0018f * std::sin(m_time * 1.7f), 0.0f} * breath;
    position += m_motion;
    position.y += m_jolt;
    rotation = rotation * Turn(-m_lag.y * 0.012f, m_lag.x * 0.012f, m_roll - m_lag.x * 0.01f);

    const float recoil = m_recoil * (1.0f - m_aim * 0.45f);
    position.z += recoil;
    rotation = rotation * Turn(recoil * 2.4f, recoil * 0.5f * m_kickSide, recoil * 0.8f * m_kickSide);

    if (weapon && weapon->IsReloading()) {
        const float t = weapon->GetReloadProgress();
        const float pose = SmoothStep(0.0f, 0.18f, t) * (1.0f - SmoothStep(0.82f, 1.0f, t));
        position += Vector3{-0.035f, -0.055f, 0.035f} * pose;
        rotation = rotation * Turn(0.16f * pose, 0.22f * pose, 0.55f * pose);
        bool magazine = false;
        bool rocket = false;
        for (const Part& part : m_partInfo) {
            magazine = magazine || StartsWith(part.name, "Magazine");
            rocket = rocket || StartsWith(part.name, "Rocket");
        }
        if (magazine || rocket) {
            // The old magazine pulled, the new one seated with a push, and the action released.
            const float seat = Pulse(t, 0.72f, 0.035f);
            position += Vector3{0.0f, 0.012f * seat - 0.007f * Pulse(t, 0.2f, 0.03f), 0.0f};
            rotation = rotation * Turn(0.07f * seat, 0.0f, 0.0f);
            const float release = Pulse(t, 0.88f, 0.025f);
            position.z += 0.01f * release;
            rotation = rotation * Turn(-0.04f * release, 0.0f, 0.0f);
        } else {
            // Shells pushed in one by one.
            for (int shell = 0; shell < 4; ++shell) {
                position.z -= 0.012f * Pulse(t, 0.27f + 0.15f * static_cast<float>(shell), 0.025f);
            }
        }
    }

    const float lowered = 1.0f - m_raise;
    position += Vector3{0.02f, -0.2f, 0.06f} * lowered;
    rotation = rotation * Turn(-0.7f * lowered, 0.0f, -0.3f * lowered);

    position += Vector3{0.03f, -0.05f, 0.08f} * m_blocked;
    rotation = rotation * Turn(-0.55f * m_blocked, -0.35f * m_blocked, 0.0f);

    return Matrix4::Translation(position) * Matrix4::Rotation(Spot::Normalize(rotation));
}

Spot::Matrix4 ViewModel::ComputePartMotion(const Part& part, bool& visible) const {
    visible = true;
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    const Weapon* weapon = holder ? holder->GetWeapon() : nullptr;
    const bool reloading = weapon && weapon->IsReloading();
    const float t = reloading ? weapon->GetReloadProgress() : 0.0f;
    if (StartsWith(part.name, "Slide")) {
        constexpr float Travel = 0.022f;
        // Empty, it stays back until the reload releases it.
        const bool locked = weapon && weapon->GetAmmo() == 0 && (!reloading || t < 0.86f);
        const float s = m_sinceShot;
        const float back =
            locked ? Travel : Travel * (s < 0.025f ? s / 0.025f : std::max(0.0f, 1.0f - (s - 0.025f) / 0.07f));
        return Matrix4::Translation({0.0f, 0.0f, back});
    }
    if (StartsWith(part.name, "Pump")) {
        const float u = (m_sinceShot - 0.18f) / 0.3f;
        const float back = u > 0.0f && u < 1.0f
                               ? 0.075f * (u < 0.5f ? SmoothStep(0.0f, 0.5f, u) : 1.0f - SmoothStep(0.5f, 1.0f, u))
                               : 0.0f;
        return Matrix4::Translation({0.0f, 0.0f, back});
    }
    if (StartsWith(part.name, "Magazine") && reloading) {
        const float out = t < 0.15f   ? 0.0f
                          : t < 0.32f ? SmoothStep(0.15f, 0.32f, t)
                          : t < 0.55f ? 1.0f
                                      : 1.0f - SmoothStep(0.55f, 0.72f, t);
        visible = out < 0.95f;
        return Matrix4::Translation(Vector3{0.0f, -0.3f, 0.05f} * out);
    }
    if (StartsWith(part.name, "Rocket") && weapon) {
        if (reloading) {
            const float in = SmoothStep(0.35f, 0.72f, t);
            visible = t > 0.35f;
            return Matrix4::Translation(Vector3{0.0f, -0.18f, -0.3f} * (1.0f - in));
        }
        visible = weapon->GetAmmo() > 0;
        return {};
    }
    if (StartsWith(part.name, "Prongs")) {
        const Vector3 axis{0.0f, m_muzzle.y, 0.0f};
        return Matrix4::Translation(axis) * Matrix4::Rotation(Quaternion::FromAxisAngle(Back, m_spin)) *
               Matrix4::Translation(-axis);
    }
    return {};
}

void ViewModel::OnLateUpdate(float deltaTime) {
    const Spot::AssetRef<Spot::Model> held = GetHeldModel();
    if (held.Get() != m_shown.Get()) {
        Show(held);
        // Each item comes up from below as it is switched to.
        auto& tweens = Spot::TweenSystem::Get(GetScene());
        tweens.Cancel(m_raiseTween);
        m_raise = 0.0f;
        m_raiseTween = tweens.Play(*this, Spot::Tween::Value(
                                              std::max(switchTime, 0.01f), 0.0f, 1.0f,
                                              [this](float value) { m_raise = value; }, Spot::Ease::CubicOut));
    }
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!player || deltaTime <= 0.0f) {
        return;
    }
    m_time += deltaTime;
    m_sinceShot = std::min(m_sinceShot + deltaTime, 10.0f);
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    m_spin = std::fmod(m_spin + deltaTime * (gun && gun->IsGrabbing() ? 10.0f : 1.2f), 2.0f * Spot::Pi);

    // Landing jolts the item down, on a damped spring.
    const auto* controller = GetEntity().GetComponent<Spot::CharacterController>();
    const bool grounded = controller && controller->IsEnabled() && controller->IsGrounded();
    if (grounded && !m_wasGrounded) {
        m_joltVelocity -= std::min(std::abs(controller->GetVelocity().y) * 0.02f + 0.08f, 0.3f);
    }
    m_wasGrounded = grounded;
    constexpr float Stiffness = 200.0f;
    const float damping = 2.0f * std::sqrt(Stiffness) * 0.7f;
    for (float left = deltaTime; left > 0.0f; left -= 1.0f / 120.0f) {
        const float step = std::min(left, 1.0f / 120.0f);
        m_joltVelocity += (-Stiffness * m_jolt - damping * m_joltVelocity) * step;
        m_jolt += m_joltVelocity * step;
    }

    const Quaternion rotation = player->GetViewRotation();
    UpdateMotion(deltaTime, rotation);
    UpdateObstruction(deltaTime);
    m_pose = ComputePose();
    const Matrix4 hand = Matrix4::Translation(player->GetEyePosition()) * Matrix4::Rotation(rotation) * m_pose;
    for (const Part& part : m_partInfo) {
        Spot::Entity* entity = GetScene().GetEntity(part.entity);
        if (!entity) {
            continue;
        }
        bool visible = true;
        // Each part where the model puts it, in the hand, moved as it moves on its own.
        const Spot::TransformComponents placed =
            Spot::Decompose(hand * ComputePartMotion(part, visible) * part.transform);
        entity->GetTransform().SetPosition(placed.translation);
        entity->GetTransform().SetRotation(placed.rotation);
        entity->GetTransform().SetScale(placed.scale);
        if (auto* renderer = entity->GetComponent<Spot::MeshRenderer>(); renderer && renderer->IsEnabled() != visible) {
            renderer->SetEnabled(visible);
        }
    }
}

void ViewModel::OnDisable() {
    if (auto* tweens = GetScene().GetSystem<Spot::TweenSystem>()) {
        tweens->Cancel(m_aimTween);
        tweens->Cancel(m_recoilTween);
        tweens->Cancel(m_raiseTween);
    }
    m_aimTarget = -1.0f;
    m_recoil = 0.0f;
    m_raise = 1.0f;
    m_motion = {};
    m_lag = {};
    m_roll = 0.0f;
    m_jolt = 0.0f;
    m_joltVelocity = 0.0f;
    m_blocked = 0.0f;
    m_hasPreviousView = false;
    Clear();
}

void ViewModel::Serialize(Spot::Archive& archive) {
    archive.Field("swayAmount", swayAmount, {.min = 0, .max = .1f, .speed = .001f});
    archive.Field("maxSway", maxSway, {.min = 0, .max = .1f, .speed = .001f});
    archive.Field("bobAmount", bobAmount, {.min = 0, .max = .1f, .speed = .001f});
    archive.Field("motionResponse", motionResponse, {.min = 0, .max = 100, .speed = .1f});
    archive.Field("switchTime", switchTime,
                  {.min = 0, .max = 2, .speed = .01f, .tooltip = "Seconds to bring an item up"});
    archive.Field("physicsGun", physicsGun, {.tooltip = "The physics gun's model"});
    archive.Field("toolGun", toolGun, {.tooltip = "The tool gun's model"});
    archive.Field("offset", offset,
                  {.speed = 0.005f, .tooltip = "Where its grip is held from the eyes: right, up, back"});
}
