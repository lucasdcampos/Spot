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
        entity.AddComponent<Spot::MeshRenderer>(part.mesh, part.material).SetCastingShadows(false);
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

void ViewModel::OnLateUpdate(float deltaTime) {
    const Spot::AssetRef<Spot::Model> held = GetHeldModel();
    if (held.Get() != m_shown.Get()) {
        Show(held);
    }
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    if (!player) {
        return;
    }
    const float target = holder && holder->IsAiming() ? 1.0f : 0.0f;
    m_aim += std::clamp(target - m_aim, -deltaTime * 8.0f, deltaTime * 8.0f);
    const Spot::Quaternion rotation = player->GetViewRotation();
    const Spot::Vector3 position = player->GetEyePosition() + rotation * (offset + (aimOffset - offset) * m_aim);
    for (const Spot::EntityId id : m_parts) {
        if (Spot::Entity* entity = GetScene().GetEntity(id)) {
            entity->GetTransform().SetPosition(position);
            entity->GetTransform().SetRotation(rotation);
        }
    }
}

void ViewModel::OnDisable() {
    Clear();
}

void ViewModel::Serialize(Spot::Archive& archive) {
    archive.Field("physicsGun", physicsGun, {.tooltip = "The physics gun's model"});
    archive.Field("toolGun", toolGun, {.tooltip = "The tool gun's model"});
    archive.Field("offset", offset, {.speed = 0.005f, .tooltip = "Where it is held from the eyes: right, up, forward"});
    archive.Field("aimOffset", aimOffset, {.speed = 0.005f, .tooltip = "Where it is held while aiming"});
}
