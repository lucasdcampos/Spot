#include "view_model.h"

#include "player_controller.h"
#include "player_movement.h"
#include "weapon.h"
#include "weapon_holder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

SPOT_COMPONENT(ViewModel);

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
    EnsureArms();
    if (!model) {
        return;
    }
    for (const Spot::ModelPart& part : model->GetParts()) {
        Spot::Entity& entity = GetScene().CreateEntity("View Model");
        // So close to the eyes, its shadow would darken the view.
        auto& renderer = entity.AddComponent<Spot::MeshRenderer>(part.mesh, part.material);
        renderer.SetCastingShadows(false);
        renderer.SetSubmesh(part.submesh);
        if (GetArmsAnimator()) {
            auto& attachment = entity.AddComponent<Spot::BoneAttachment>();
            attachment.SetAnimator(m_armsEntity);
            attachment.SetBone("RightHand");
            attachment.SetOffset(Spot::Decompose(Spot::Matrix4::Translation(gripOffset) * part.transform));
        }
        m_parts.push_back(entity.GetId());
    }
}

void ViewModel::OnStart() {
    if (!arms) {
        if (auto* assets = Spot::Assets::GetManager(); assets && assets->FindByPath("generated/models/arms.gltf")) {
            arms = assets->Load<Spot::Model>("generated/models/arms.gltf");
        }
    }
    EnsureArms();
}
Spot::Animator* ViewModel::GetArmsAnimator() const {
    auto* entity = GetScene().GetEntity(m_armsEntity);
    return entity ? entity->GetComponent<Spot::Animator>() : nullptr;
}
void ViewModel::EnsureArms() {
    if (!arms || GetScene().GetEntity(m_armsEntity)) {
        return;
    }
    auto& root = GetScene().Instantiate(*arms, "Animated Arms");
    m_armsEntity = root.GetId();
    root.SetLayer(GetEntity().GetLayer());
    GetScene().ForEachComponent<Spot::SkinnedMeshRenderer>([&](Spot::SkinnedMeshRenderer& renderer) {
        if (renderer.GetAnimator() == m_armsEntity) {
            renderer.SetCastingShadows(false);
        }
    });
}
bool ViewModel::Animate(std::string_view action, float duration) {
    if (action != "switch" && GetHeldModel().Get() != m_shown.Get()) {
        Show(GetHeldModel());
    }
    auto* animator = GetArmsAnimator();
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    if (!animator || !holder || !arms) {
        return false;
    }
    constexpr std::array<std::string_view, 6> Names{"physics_gun", "pistol",          "rifle",
                                                    "shotgun",     "rocket_launcher", "tool_gun"};
    const auto slot = static_cast<std::size_t>(std::clamp(holder->GetSelected(), 0, 5));
    const auto name =
        action == "idle" ? std::string("arms_idle") : std::string(Names[slot]) + "_" + std::string(action);
    for (const auto& node : arms->GetNodes()) {
        for (const auto& clip : node.animations) {
            if (clip && clip->name == name) {
                animator->SetSpeed(duration > 0 ? clip->duration / duration : 1);
                return animator->Play(clip, action == "idle");
            }
        }
    }
    return false;
}

void ViewModel::Clear() {
    for (const Spot::EntityId id : m_parts) {
        if (Spot::Entity* entity = GetScene().GetEntity(id)) {
            entity->Destroy();
        }
    }
    m_parts.clear();
    m_shown = {};
}

void ViewModel::Kick(float strength) {
    Animate("fire");
    auto& tweens = Spot::TweenSystem::Get(GetScene());
    tweens.Cancel(m_recoilTween);
    const float peak = std::clamp(m_recoil + strength, 0.0f, 0.25f);
    const auto set = [this](float value) { m_recoil = value; };
    m_recoilTween =
        tweens.Play(*this, Spot::Tween::Sequence({Spot::Tween::Value(0.03f, m_recoil, peak, set, Spot::Ease::QuadOut),
                                                  Spot::Tween::Value(0.16f, peak, 0.0f, set, Spot::Ease::SineInOut)}));
}

void ViewModel::OnUpdate(float /*deltaTime*/) {
    if (auto* animator = GetArmsAnimator(); animator && !animator->IsPlaying()) {
        Animate("idle");
    }
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    const float target = holder && holder->IsAiming() ? 1.0f : 0.0f;
    if (target != m_aimTarget) {
        m_aimTarget = target;
        auto& tweens = Spot::TweenSystem::Get(GetScene());
        tweens.Cancel(m_aimTween);
        m_aimTween = tweens.Play(
            *this,
            Spot::Tween::Value(0.125f, m_aim, target, [this](float value) { m_aim = value; }, Spot::Ease::QuadOut));
    }
}

void ViewModel::UpdateMotion(float deltaTime, const Spot::Quaternion& view) {
    if (deltaTime <= 0) {
        return;
    }
    Spot::Vector3 target;
    if (m_hasPreviousView && Spot::Input::IsCursorLocked()) {
        auto change = Spot::Inverse(m_previousView) * view;
        if (change.w < 0) {
            change = {-change.x, -change.y, -change.z, -change.w};
        }
        target.x = std::clamp(2 * change.y * swayAmount / deltaTime, -maxSway, maxSway);
        target.y = std::clamp(-2 * change.x * swayAmount / deltaTime, -maxSway, maxSway);
        const auto* controller = GetEntity().GetComponent<Spot::CharacterController>();
        const auto* movement = GetEntity().GetComponent<PlayerMovement>();
        if (controller && movement && controller->IsGrounded() && !movement->IsFlying()) {
            auto velocity = controller->GetVelocity();
            velocity.y = 0;
            const float speed = Spot::Length(velocity);
            m_bobPhase = std::fmod(m_bobPhase + speed * deltaTime * 8, 2 * Spot::Pi);
            const float amount = bobAmount * std::clamp(speed / std::max(movement->walkSpeed, .1f), 0.0f, 1.5f);
            target += Spot::Vector3{std::sin(m_bobPhase) * amount * .5f, std::cos(m_bobPhase * 2) * amount, 0};
        }
    }
    m_previousView = view;
    m_hasPreviousView = true;
    target = target * (1 - m_aim * .8f);
    m_motion += (target - m_motion) * (1 - std::exp(-motionResponse * deltaTime));
}

void ViewModel::OnLateUpdate(float deltaTime) {
    const Spot::AssetRef<Spot::Model> held = GetHeldModel();
    if (held.Get() != m_shown.Get()) {
        Show(held);
        Animate("switch");
    }
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!player) {
        return;
    }
    const Spot::Quaternion rotation = player->GetViewRotation();
    UpdateMotion(deltaTime, rotation);
    const Spot::Vector3 position =
        player->GetEyePosition() +
        rotation * (offset + (aimOffset - offset) * m_aim + Spot::Vector3{0.0f, 0.0f, m_recoil} + m_motion);
    if (auto* root = GetScene().GetEntity(m_armsEntity)) {
        root->GetTransform().SetWorldPosition(
            player->GetEyePosition() +
            rotation * ((aimOffset - offset) * m_aim + Spot::Vector3{0, 0, m_recoil} + m_motion));
        root->GetTransform().SetWorldRotation(rotation);
    }
    const Spot::Matrix4 hand = Spot::Matrix4::Translation(position) * Spot::Matrix4::Rotation(rotation);
    const auto parts = m_shown ? m_shown->GetParts() : std::span<const Spot::ModelPart>();
    for (std::size_t i = 0; i < m_parts.size() && i < parts.size(); ++i) {
        if (Spot::Entity* entity = GetScene().GetEntity(m_parts[i])) {
            // Each part where the model puts it, in the hand.
            const Spot::TransformComponents placed = Spot::Decompose(hand * parts[i].transform);
            entity->GetTransform().SetPosition(placed.translation);
            entity->GetTransform().SetRotation(placed.rotation);
            entity->GetTransform().SetScale(placed.scale);
        }
    }
}

void ViewModel::OnDisable() {
    if (auto* tweens = GetScene().GetSystem<Spot::TweenSystem>()) {
        tweens->Cancel(m_aimTween);
        tweens->Cancel(m_recoilTween);
    }
    m_aimTarget = -1.0f;
    m_recoil = 0.0f;
    m_motion = {};
    m_hasPreviousView = false;
    m_bobPhase = 0;
    Clear();
    if (auto* root = GetScene().GetEntity(m_armsEntity)) {
        root->Destroy();
    }
    m_armsEntity = Spot::EntityId::Invalid;
}

void ViewModel::Serialize(Spot::Archive& archive) {
    archive.Field("swayAmount", swayAmount, {.min = 0, .max = .1f, .speed = .001f});
    archive.Field("maxSway", maxSway, {.min = 0, .max = .1f, .speed = .001f});
    archive.Field("bobAmount", bobAmount, {.min = 0, .max = .1f, .speed = .001f});
    archive.Field("motionResponse", motionResponse, {.min = 0, .max = 100, .speed = .1f});
    archive.Field("arms", arms, {.tooltip = "The rig whose right hand holds the weapon"});
    archive.Field("gripOffset", gripOffset, {.speed = 0.005f, .tooltip = "The weapon origin relative to the hand"});
    archive.Field("physicsGun", physicsGun, {.tooltip = "The physics gun's model"});
    archive.Field("toolGun", toolGun, {.tooltip = "The tool gun's model"});
    archive.Field("offset", offset, {.speed = 0.005f, .tooltip = "Where it is held from the eyes: right, up, forward"});
    archive.Field("aimOffset", aimOffset, {.speed = 0.005f, .tooltip = "Where it is held while aiming"});
}
