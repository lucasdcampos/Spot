#include "weapon_holder.h"

#include "explosive_barrel.h"
#include "physics_gun.h"
#include "player_controller.h"
#include "projectile.h"
#include "prop_carrier.h"
#include "weapon.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(WeaponHolder);

void WeaponHolder::Select(int slot) {
    const int count = static_cast<int>(GetWeapons().size()) + 1;
    m_selected = ((slot % count) + count) % count;
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
    for (int pellet = 0; pellet < weapon.pellets; ++pellet) {
        const Spot::Vector3 direction = Scatter(aim, weapon.spread);
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
    using Spot::Key;
    const std::vector<Weapon*> weapons = GetWeapons();
    for (Weapon* weapon : weapons) {
        weapon->Tick(deltaTime);
    }

    constexpr Key Slots[] = {Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5,
                             Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9};
    for (int slot = 0; slot < 9 && slot <= static_cast<int>(weapons.size()); ++slot) {
        if (Input::IsKeyPressed(Slots[slot])) {
            Select(slot);
        }
    }
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    const float scroll = Input::GetScrollDelta().y;
    if (scroll != 0.0f && !(gun && gun->IsGrabbing())) {
        Select(m_selected + (scroll > 0.0f ? -1 : 1));
    }

    Weapon* weapon = GetWeapon();
    if (!weapon) {
        return;
    }
    if (Input::IsKeyPressed(Key::R)) {
        weapon->StartReload();
    }
    // A carried prop takes the click, to be thrown.
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    const bool firing = weapon->automatic ? Input::IsMouseButtonDown(Spot::MouseButton::Left)
                                          : Input::IsMouseButtonPressed(Spot::MouseButton::Left);
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
