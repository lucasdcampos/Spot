#include "weapon_holder.h"
#include <spot/scene/audio/audio_system.h>
#include <spot/scene/physics/physics_world.h>

#include "explosive_barrel.h"
#include "flash.h"
#include "particle_effects.h"
#include "physics_gun.h"
#include "player_controller.h"
#include "player_health.h"
#include "player_movement.h"
#include "projectile.h"
#include "prop_carrier.h"
#include "prop_spawner.h"
#include "robot.h"
#include "sound_bank.h"
#include "view_model.h"
#include "water.h"
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

bool WeaponHolder::CanUseItem() const {
    const auto* movement = GetEntity().GetComponent<PlayerMovement>();
    const auto* health = GetEntity().GetComponent<PlayerHealth>();
    // Placing a prop from the spawn menu takes the buttons.
    const auto* spawner = GetEntity().GetComponent<PropSpawner>();
    return IsActiveAndEnabled() && !m_wheelOpen && !(movement && movement->IsSprinting()) &&
           !(health && health->IsDead()) && !(spawner && spawner->HasClick());
}

void WeaponHolder::OpenWheel() {
    if (m_wheelOpen) {
        return;
    }
    m_wheelOpen = true;
    m_wheelSlot = m_selected;
    m_wheelPointer = {};
    m_timeScaleBefore = Spot::Time::GetTimeScale();
    m_timeScaleSet = m_timeScaleBefore * std::clamp(wheelTimeScale, 0.0f, 1.0f);
    Spot::Time::SetTimeScale(m_timeScaleSet);
    SoundBank::PlayClick(GetScene());
}

void WeaponHolder::CloseWheel(bool select) {
    if (!m_wheelOpen) {
        return;
    }
    m_wheelOpen = false;
    // Unless something else, such as the pause menu, changed it meanwhile.
    if (Spot::Time::GetTimeScale() == m_timeScaleSet) {
        Spot::Time::SetTimeScale(m_timeScaleBefore);
    }
    if (select) {
        Select(m_wheelSlot);
    }
}

void WeaponHolder::UpdateWheel() {
    // A few centimeters of mouse reach the rim; the pointer stays on it as the mouse goes on.
    m_wheelPointer += Spot::Input::GetActionVector2("Look") / 90.0f;
    const float length = Spot::Length(m_wheelPointer);
    if (length > 1.0f) {
        m_wheelPointer = m_wheelPointer / length;
    }
    if (length < 0.4f) {
        return;
    }
    // Clockwise from the top, in eighths of a turn.
    const float turn = std::atan2(m_wheelPointer.x, m_wheelPointer.y) / (2.0f * Spot::Pi);
    const int slot = (static_cast<int>(std::lround(turn * WheelSlots)) % WheelSlots + WheelSlots) % WheelSlots;
    if (slot < GetSlotCount() && slot != m_wheelSlot) {
        m_wheelSlot = slot;
        SoundBank::PlayClick(GetScene());
    }
}

void WeaponHolder::PlayWeaponSound(const Spot::AssetRef<Spot::Sound>& sound, float volume) {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!sound || !player) {
        return;
    }
    // Close to full volume for the player holding it, and heard far around.
    GetScene().GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
        sound.GetShared(), player->GetEyePosition(),
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
    if (!weapon || !player || !CanUseItem() || !weapon->TakeShot()) {
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
    const float flash = std::clamp(weapon->muzzleFlash, 0.1f, 4.0f);
    Flash::Spawn(GetScene(), muzzle + aim * 0.05f, {1.0f, 0.75f, 0.4f, 1.0f}, 5.0f * flash, 12.0f, 0.05f, 0.3f);
    ParticleEffects::Muzzle(GetScene(), muzzle, aim, flash);
    if (weapon->kind == Weapon::Kind::Projectile) {
        // From the muzzle toward what the crosshair is on, unless a wall is closer than the muzzle.
        const float reach = 200.0f;
        const auto target =
            GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(eye, aim, reach, {.ignore = GetEntity().GetId()});
        const Spot::Vector3 point = target ? target->point : eye + aim * reach;
        const float toMuzzle = Spot::Distance(muzzle, eye);
        const auto blocked = toMuzzle > 0.01f
                                 ? GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(
                                       eye, (muzzle - eye) / toMuzzle, toMuzzle, {.ignore = GetEntity().GetId()})
                                 : std::nullopt;
        const bool clear = toMuzzle > 0.01f && !blocked && (!target || target->distance > 1.5f);
        const Spot::Vector3 start = clear ? muzzle : eye + aim * 0.5f;
        const Spot::Vector3 direction = clear ? Spot::Normalize(point - start) : aim;
        Spot::Entity& rocket =
            Projectile::Launch(GetScene(), start, direction, weapon->projectileSpeed, GetEntity().GetId());
        auto& projectile = *rocket.GetComponent<Projectile>();
        projectile.explosionRadius = weapon->explosionRadius;
        projectile.explosionImpulse = weapon->explosionImpulse;
        projectile.explosionDamage = weapon->explosionDamage;
    } else {
        FireHitscan(*weapon, eye, aim, muzzle);
    }
    player->Kick(weapon->cameraRecoil * (m_aiming ? 0.65f : 1.0f), weapon->recoilRecovery);
}

void WeaponHolder::FireHitscan(const Weapon& weapon, Spot::Vector3 eye, Spot::Vector3 aim, Spot::Vector3 muzzle) {
    const float spread = m_aiming ? weapon.spread * weapon.aimSpread : weapon.spread;
    for (int pellet = 0; pellet < weapon.pellets; ++pellet) {
        const Spot::Vector3 direction = Scatter(aim, spread);
        const auto hit = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(eye, direction, weapon.range,
                                                                                 {.ignore = GetEntity().GetId()});
        const auto water = Water::Raycast(GetScene(), eye, direction, hit ? hit->distance : weapon.range);
        // A streak of light from the muzzle to where the pellet went; every other one of a spread is enough.
        if (pellet % 2 == 0) {
            ParticleEffects::Tracer(GetScene(), muzzle,
                                    water ? water->point
                                    : hit ? hit->point
                                          : eye + direction * weapon.range);
        }
        // Water stops shots soon: what is deeper is out of reach, and what is close below gets less of a push.
        float push = 1.0f;
        if (water) {
            water->water->Splash(water->point, 0.25f / std::sqrt(static_cast<float>(std::max(weapon.pellets, 1))));
            const float under = hit ? hit->distance - water->distance : weapon.range;
            if (under > water->water->shotDepth) {
                continue;
            }
            push = 0.3f * (1.0f - under / std::max(water->water->shotDepth, 0.01f));
        }
        if (!hit) {
            continue;
        }
        Spot::Entity* entity = GetScene().GetEntity(hit->entity);
        if (!entity) {
            continue;
        }
        if (!water) {
            ParticleEffects::Impact(GetScene(), hit->point, hit->normal, Surface::Of(entity));
        }
        if (Robots::Hit(GetScene(), hit->entity, direction * weapon.impulse * push, hit->point)) {
            m_hitMarker = 1.0f;
            continue;
        }
        if (auto* barrel = entity->GetComponent<ExplosiveBarrel>()) {
            barrel->Ignite();
        }
        if (auto* body = entity->GetComponent<Spot::Rigidbody>(); body && !body->IsKinematic()) {
            m_hitMarker = 1.0f;
            body->AddImpulseAtPosition(direction * weapon.impulse * push, hit->point);
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
    m_hitMarker = std::max(m_hitMarker - deltaTime * 4.0f, 0.0f);
    const int count = GetWeaponCount();

    static constexpr std::array<std::string_view, 9> Slots = {
        "Weapon 1", "Weapon 2", "Weapon 3", "Weapon 4", "Weapon 5", "Weapon 6", "Weapon 7", "Weapon 8", "Weapon 9"};
    for (int slot = 0; slot < 9 && slot <= count + 1; ++slot) {
        if (Input::IsActionPressed(Slots[static_cast<std::size_t>(slot)])) {
            Select(slot);
            m_wheelSlot = m_selected;
        }
    }
    // The wheel is for the game, not over a menu, which takes it away without choosing.
    const Spot::InputState* state = Input::GetState();
    const bool inGame = Input::IsCursorLocked() && !(state && state->IsCaptured());
    if (!m_wheelOpen && inGame && Input::IsActionDown("Weapon Wheel")) {
        OpenWheel();
    } else if (m_wheelOpen && !inGame) {
        CloseWheel(false);
    } else if (m_wheelOpen && !Input::IsActionDown("Weapon Wheel")) {
        CloseWheel(true);
    }
    if (m_wheelOpen) {
        UpdateWheel();
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
    const auto* health = GetEntity().GetComponent<PlayerHealth>();
    const auto* spawner = GetEntity().GetComponent<PropSpawner>();
    m_aiming = weapon && Input::IsActionDown("Aim") && !(carrier && carrier->IsHolding()) && !weapon->IsReloading() &&
               !m_wheelOpen && !(health && health->IsDead()) && !(spawner && spawner->HasClick());
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

void WeaponHolder::OnDisable() {
    CloseWheel(false);
}

void WeaponHolder::Serialize(Spot::Archive& archive) {
    archive.Field("selected", m_selected, {.min = 0.0f, .max = 9.0f, .tooltip = "0 is the physics gun"});
    archive.Field("wheelTimeScale", wheelTimeScale,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "How much the open wheel slows time"});
}
