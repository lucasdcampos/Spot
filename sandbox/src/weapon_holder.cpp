#include "weapon_holder.h"

#include "explosive_barrel.h"
#include "flash.h"
#include "particle_effects.h"
#include "physics_gun.h"
#include "player_controller.h"
#include "projectile.h"
#include "prop_carrier.h"
#include "robot.h"
#include "sound_bank.h"
#include "view_model.h"
#include "weapon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

SPOT_COMPONENT(WeaponHolder);

void WeaponHolder::Select(int slot) {
    const int count = GetWeaponCount() + 2;
    const int selected = ((slot % count) + count) % count;
    if (selected != m_selected) {
        m_selected = selected;
        SoundBank::PlayClick(GetScene());
    }
}

void WeaponHolder::PlayWeaponSound(const Spot::AssetRef<Spot::Sound>& sound, float volume) {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!sound || !player) {
        return;
    }
    // Close to full volume for the player holding it, and heard far around.
    GetScene().GetAudio().PlayOneShot(sound.GetShared(), player->GetEyePosition(),
                                      {.volume = volume,
                                       .bus = "Effects",
                                       .spatialSettings = {.minDistance = 3.0f, .maxDistance = 100.0f},
                                       .priority = 5,
                                       .maxInstances = 4});
}

void WeaponHolder::StartReload(Weapon& weapon) {
    if (weapon.StartReload()) {
        PlayWeaponSound(weapon.reloadSound, 0.8f);
    }
}

float WeaponHolder::GetAimZoom() const {
    const Weapon* weapon = m_aiming ? GetWeapon() : nullptr;
    return weapon ? weapon->aimZoom : 1.0f;
}

Weapon* WeaponHolder::GetWeapon() const {
    Weapon* selected = nullptr;
    int slot = 0;
    GetEntity().ForEachChild([&](const Spot::Entity& child) {
        if (auto* weapon = child.GetComponent<Weapon>(); weapon && ++slot == m_selected) {
            selected = weapon;
        }
    });
    return selected;
}

int WeaponHolder::GetWeaponCount() const {
    int count = 0;
    GetEntity().ForEachChild([&](const Spot::Entity& child) { count += child.HasComponent<Weapon>() ? 1 : 0; });
    return count;
}

std::vector<Weapon*> WeaponHolder::GetWeapons() const {
    std::vector<Weapon*> weapons;
    GetEntity().ForEachChild([&](const Spot::Entity& child) {
        if (Weapon* weapon = child.GetComponent<Weapon>()) {
            weapons.push_back(weapon);
        }
    });
    return weapons;
}

void WeaponHolder::Fire() {
    Weapon* weapon = GetWeapon();
    auto* player = GetEntity().GetComponent<PlayerController>();
    if (!weapon || !player || !weapon->TakeShot()) {
        return;
    }
    auto* view = GetEntity().GetComponent<ViewModel>();
    // Shots aim from the eyes, where the crosshair is, and are seen leaving the muzzle as it is drawn.
    const Spot::Vector3 eye = player->GetEyePosition();
    const Spot::Vector3 aim = player->GetAimDirection();
    const Spot::Vector3 muzzle = view ? view->GetMuzzlePosition() : eye;
    if (view) {
        view->Kick(weapon->viewRecoil);
    }
    Spot::Input::RumbleGamepad(0.2f, weapon->kind == Weapon::Kind::Projectile ? 0.8f : 0.5f, 0.08f);
    PlayWeaponSound(weapon->fireSound, 1.0f);
    Flash::Spawn(GetScene(), muzzle + aim * 0.05f, {1.0f, 0.75f, 0.4f, 1.0f}, 0.12f, 5.0f, 12.0f, 0.05f);
    ParticleEffects::Muzzle(GetScene(), muzzle, aim);
    if (weapon->kind == Weapon::Kind::Projectile) {
        // From the muzzle toward what the crosshair is on, unless a wall is closer than the muzzle.
        const float reach = 200.0f;
        const auto target = GetScene().GetPhysics().Raycast(eye, aim, reach, {.ignore = GetEntity().GetId()});
        const Spot::Vector3 point = target ? target->point : eye + aim * reach;
        const float toMuzzle = Spot::Distance(muzzle, eye);
        const auto blocked = toMuzzle > 0.01f
                                 ? GetScene().GetPhysics().Raycast(eye, (muzzle - eye) / toMuzzle, toMuzzle,
                                                                   {.ignore = GetEntity().GetId()})
                                 : std::nullopt;
        const bool clear = toMuzzle > 0.01f && !blocked && (!target || target->distance > 1.5f);
        const Spot::Vector3 start = clear ? muzzle : eye + aim * 0.5f;
        const Spot::Vector3 direction = clear ? Spot::Normalize(point - start) : aim;
        Spot::Entity& rocket =
            Projectile::Launch(GetScene(), start, direction, weapon->projectileSpeed, GetEntity().GetId());
        auto& projectile = *rocket.GetComponent<Projectile>();
        projectile.explosionRadius = weapon->explosionRadius;
        projectile.explosionImpulse = weapon->explosionImpulse;
    } else {
        FireHitscan(*weapon, eye, aim, muzzle);
    }
    player->Kick(weapon->cameraRecoil * (m_aiming ? 0.65f : 1.0f), weapon->recoilRecovery);
}

void WeaponHolder::FireHitscan(const Weapon& weapon, Spot::Vector3 eye, Spot::Vector3 aim, Spot::Vector3 muzzle) {
    const float spread = m_aiming ? weapon.spread * weapon.aimSpread : weapon.spread;
    for (int pellet = 0; pellet < weapon.pellets; ++pellet) {
        const Spot::Vector3 direction = Scatter(aim, spread);
        const auto hit = GetScene().GetPhysics().Raycast(eye, direction, weapon.range, {.ignore = GetEntity().GetId()});
        // A streak of light from the muzzle to where the pellet went; every other one of a spread is enough.
        if (pellet % 2 == 0) {
            ParticleEffects::Tracer(GetScene(), muzzle, hit ? hit->point : eye + direction * weapon.range);
        }
        if (!hit) {
            continue;
        }
        Spot::Entity* entity = GetScene().GetEntity(hit->entity);
        if (!entity) {
            continue;
        }
        ParticleEffects::Impact(GetScene(), hit->point, hit->normal, Surface::Of(entity));
        if (Robots::Hit(GetScene(), hit->entity, direction * weapon.impulse, hit->point)) {
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

void WeaponHolder::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    const int count = GetWeaponCount();

    static constexpr std::array<std::string_view, 9> Slots = {
        "Weapon 1", "Weapon 2", "Weapon 3", "Weapon 4", "Weapon 5", "Weapon 6", "Weapon 7", "Weapon 8", "Weapon 9"};
    for (int slot = 0; slot < 9 && slot <= count + 1; ++slot) {
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
    // Both hands are busy reloading.
    m_aiming = weapon && Input::IsActionDown("Aim") && !(carrier && carrier->IsHolding()) && !weapon->IsReloading();
    if (!weapon) {
        return;
    }
    if (Input::IsActionPressed("Reload")) {
        StartReload(*weapon);
    }
    const bool firing = weapon->automatic ? Input::IsActionDown("Fire") : Input::IsActionPressed("Fire");
    if (firing && Input::IsCursorLocked() && !(carrier && carrier->HasClick())) {
        Fire();
    }
    // Out of rounds, it reloads by itself.
    if (weapon->GetAmmo() == 0) {
        StartReload(*weapon);
    }
}

void WeaponHolder::Serialize(Spot::Archive& archive) {
    archive.Field("selected", m_selected, {.min = 0.0f, .max = 9.0f, .tooltip = "0 is the physics gun"});
}
