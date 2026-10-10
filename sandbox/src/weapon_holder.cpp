#include "weapon_holder.h"

#include "explosive_barrel.h"
#include "physics_gun.h"
#include "player_controller.h"
#include "projectile.h"
#include "prop_carrier.h"
#include "weapon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

SPOT_COMPONENT(WeaponHolder);

void WeaponHolder::Select(int slot) {
    const int count = static_cast<int>(GetWeapons().size()) + 2;
    m_selected = ((slot % count) + count) % count;
}

float WeaponHolder::GetAimZoom() const {
    const Weapon* weapon = m_aiming ? GetWeapon() : nullptr;
    return weapon ? weapon->aimZoom : 1.0f;
}

Weapon* WeaponHolder::GetWeapon() const {
    const std::vector<Weapon*> weapons = GetWeapons();
    return m_selected > 0 && m_selected <= static_cast<int>(weapons.size()) ? weapons[m_selected - 1] : nullptr;
}

std::vector<Weapon*> WeaponHolder::GetWeapons() const {
    std::vector<Weapon*> weapons;
    for (const Spot::Entity* child : GetEntity().GetChildren()) {
        if (Weapon* weapon = child->GetComponent<Weapon>()) {
            weapons.push_back(weapon);
        }
    }
    return weapons;
}

void WeaponHolder::Fire() {
    Weapon* weapon = GetWeapon();
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!weapon || !player || !weapon->TakeShot()) {
        return;
    }
    Spot::Input::RumbleGamepad(0.2f, weapon->kind == Weapon::Kind::Projectile ? 0.8f : 0.5f, 0.08f);
    const Spot::Vector3 eye = player->GetEyePosition();
    const Spot::Vector3 aim = player->GetAimDirection();
    if (weapon->kind == Weapon::Kind::Projectile) {
        Spot::Entity& rocket =
            Projectile::Launch(GetScene(), eye + aim * 1.0f, aim, weapon->projectileSpeed, GetEntity().GetId());
        auto& projectile = *rocket.GetComponent<Projectile>();
        projectile.explosionRadius = weapon->explosionRadius;
        projectile.explosionImpulse = weapon->explosionImpulse;
    } else {
        FireHitscan(*weapon, eye, aim);
    }
}

void WeaponHolder::FireHitscan(const Weapon& weapon, Spot::Vector3 eye, Spot::Vector3 aim) {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const Spot::Vector3 muzzle = eye + player->GetViewRotation() * Spot::Vector3{0.2f, -0.15f, -0.3f};
    const float spread = m_aiming ? weapon.spread * weapon.aimSpread : weapon.spread;
    for (int pellet = 0; pellet < weapon.pellets; ++pellet) {
        const Spot::Vector3 direction = Scatter(aim, spread);
        const auto hit = GetScene().GetPhysics().Raycast(eye, direction, weapon.range, {.ignore = GetEntity().GetId()});
        // The tracer, until weapons get muzzle flashes and impacts of their own.
        Spot::DebugDraw::Line(muzzle, hit ? hit->point : eye + direction * weapon.range,
                              {.color = {1.0f, 0.9f, 0.5f, 1.0f}, .duration = 0.05f});
        if (!hit) {
            continue;
        }
        Spot::Entity* entity = GetScene().GetEntity(hit->entity);
        if (!entity) {
            continue;
        }
        if (auto* barrel = entity->GetComponent<ExplosiveBarrel>()) {
            barrel->Ignite();
        }
        if (auto* body = entity->GetComponent<Spot::Rigidbody>(); body && !body->IsKinematic()) {
            body->AddImpulseAtPosition(direction * weapon.impulse, hit->point);
        }
    }
}

Spot::Vector3 WeaponHolder::Scatter(Spot::Vector3 aim, float spread) {
    if (spread <= 0.0f) {
        return aim;
    }
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    // Even over the cone's disc: the square root keeps pellets from bunching at the middle.
    const float radius = std::tan(spread) * std::sqrt(unit(m_random));
    const float angle = 2.0f * Spot::Pi * unit(m_random);
    const Spot::Vector3 side = Spot::Normalize(
        Spot::Cross(aim, std::abs(aim.y) < 0.99f ? Spot::Vector3{0.0f, 1.0f, 0.0f} : Spot::Vector3{1.0f, 0.0f, 0.0f}));
    const Spot::Vector3 up = Spot::Cross(side, aim);
    return Spot::Normalize(aim + (side * std::cos(angle) + up * std::sin(angle)) * radius);
}

void WeaponHolder::OnUpdate(float deltaTime) {
    namespace Input = Spot::Input;
    const std::vector<Weapon*> weapons = GetWeapons();
    for (Weapon* weapon : weapons) {
        weapon->Tick(deltaTime);
    }

    static constexpr std::array<std::string_view, 9> Slots = {
        "Weapon 1", "Weapon 2", "Weapon 3", "Weapon 4", "Weapon 5", "Weapon 6", "Weapon 7", "Weapon 8", "Weapon 9"};
    for (int slot = 0; slot < 9 && slot <= static_cast<int>(weapons.size()) + 1; ++slot) {
        if (Input::IsActionPressed(Slots[static_cast<std::size_t>(slot)])) {
            Select(slot);
        }
    }
    // The physics gun takes the wheel while it holds something, to push and pull it.
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    if (Input::IsActionPressed("Switch Weapon") && !(gun && gun->IsGrabbing())) {
        Select(m_selected + (Input::GetActionValue("Switch Weapon") > 0.0f ? 1 : -1));
    }

    Weapon* weapon = GetWeapon();
    // A carried prop takes the buttons, to be turned and thrown.
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    m_aiming = weapon && Input::IsActionDown("Aim") && !(carrier && carrier->IsHolding());
    if (!weapon) {
        return;
    }
    if (Input::IsActionPressed("Reload")) {
        weapon->StartReload();
    }
    const bool firing = weapon->automatic ? Input::IsActionDown("Fire") : Input::IsActionPressed("Fire");
    if (firing && Input::IsCursorLocked() && !(carrier && carrier->HasClick())) {
        Fire();
    }
    // Out of rounds, it reloads by itself.
    if (weapon->GetAmmo() == 0) {
        weapon->StartReload();
    }
}

void WeaponHolder::Serialize(Spot::Archive& archive) {
    archive.Field("selected", m_selected, {.min = 0.0f, .max = 9.0f, .tooltip = "0 is the physics gun"});
}
