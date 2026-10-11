#include "view_model.h"

#include "player_controller.h"
#include "weapon.h"
#include "weapon_holder.h"

#include <algorithm>

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
    if (!model) {
        return;
    }
    for (const Spot::ModelPart& part : model->GetParts()) {
        Spot::Entity& entity = GetScene().CreateEntity("View Model");
        // So close to the eyes, its shadow would darken the view.
        auto& renderer = entity.AddComponent<Spot::MeshRenderer>(part.mesh, part.material);
        renderer.SetCastingShadows(false);
        renderer.SetSubmesh(part.submesh);
        m_parts.push_back(entity.GetId());
    }
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
    auto& tweens = Spot::TweenSystem::Get(GetScene());
    tweens.Cancel(m_recoilTween);
    const float peak = std::clamp(m_recoil + strength, 0.0f, 0.25f);
    const auto set = [this](float value) { m_recoil = value; };
    m_recoilTween =
        tweens.Play(*this, Spot::Tween::Sequence({Spot::Tween::Value(0.03f, m_recoil, peak, set, Spot::Ease::QuadOut),
                                                  Spot::Tween::Value(0.16f, peak, 0.0f, set, Spot::Ease::SineInOut)}));
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
            Spot::Tween::Value(0.125f, m_aim, target, [this](float value) { m_aim = value; }, Spot::Ease::QuadOut));
    }
}

void ViewModel::OnLateUpdate(float /*deltaTime*/) {
    const Spot::AssetRef<Spot::Model> held = GetHeldModel();
    if (held.Get() != m_shown.Get()) {
        Show(held);
    }
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!player) {
        return;
    }
    const Spot::Quaternion rotation = player->GetViewRotation();
    const Spot::Vector3 position = player->GetEyePosition() + rotation * (offset + (aimOffset - offset) * m_aim +
                                                                          Spot::Vector3{0.0f, 0.0f, m_recoil});
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
    Clear();
}

void ViewModel::Serialize(Spot::Archive& archive) {
    archive.Field("physicsGun", physicsGun, {.tooltip = "The physics gun's model"});
    archive.Field("toolGun", toolGun, {.tooltip = "The tool gun's model"});
    archive.Field("offset", offset, {.speed = 0.005f, .tooltip = "Where it is held from the eyes: right, up, forward"});
    archive.Field("aimOffset", aimOffset, {.speed = 0.005f, .tooltip = "Where it is held while aiming"});
}
