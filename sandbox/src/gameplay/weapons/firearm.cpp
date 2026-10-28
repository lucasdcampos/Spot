#include "gameplay/weapons/firearm.h"
#include <spot/scene/audio/audio_system.h>
#include <spot/scene/physics/physics_world.h>

#include "gameplay/camera/first_person_view.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/trace_volume.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/effects/surface.h"
#include "gameplay/health/damage.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/inventory/view_model.h"
#include "gameplay/inventory/world_model.h"
#include "gameplay/weapons/projectile.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Gameplay {

SPOT_COMPONENT(Firearm);

Firearm::Firearm() : HeldItem({"Fire", "Aim", "Reload"}) {
    displayName = "Weapon";
}

bool Firearm::IsReady() const {
    return m_ammo > 0 && !IsReloading() && !IsInvokePending(m_cooldown);
}

float Firearm::GetReloadProgress() const {
    if (!IsReloading() || reloadTime <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(1.0f - GetInvokeTimeLeft(m_reload) / reloadTime, 0.0f, 1.0f);
}

bool Firearm::TakeShot() {
    if (!IsReady()) {
        return false;
    }
    --m_ammo;
    m_cooldown = fireInterval > 0.0f ? Invoke(fireInterval, [] {}) : Spot::InvokeId::Invalid;
    return true;
}

bool Firearm::StartReload() {
    if (IsReloading() || m_ammo >= magazineSize || m_reserve <= 0) {
        return false;
    }
    ScheduleReload(reloadTime);
    return true;
}

void Firearm::Refill() {
    CancelInvoke(m_cooldown);
    CancelInvoke(m_reload);
    m_cooldown = Spot::InvokeId::Invalid;
    m_reload = Spot::InvokeId::Invalid;
    m_ammo = std::max(0, magazineSize);
    m_reserve = std::max(0, reserveAmmo);
    m_savedCooldown = 0.0f;
    m_savedReload = 0.0f;
    m_savedReloadPending = false;
}

void Firearm::ScheduleReload(float seconds) {
    m_reload = Invoke(std::max(0.0f, seconds), [this] {
        const int loaded = std::min(magazineSize - m_ammo, m_reserve);
        m_ammo += loaded;
        m_reserve -= loaded;
    });
}

void Firearm::BeginReload() {
    if (StartReload()) {
        PlaySound(reloadSound, 0.8f);
    }
}

void Firearm::SetAiming(bool aiming) {
    if (aiming == m_aiming) {
        return;
    }
    m_aiming = aiming;
    if (Pawn* pawn = GetPawn()) {
        pawn->SetState("Aiming", aiming);
    }
}

float Firearm::GetZoom() const {
    return m_aiming ? aimZoom : 1.0f;
}

void Firearm::PlaySound(const Spot::AssetRef<Spot::Sound>& sound, float volume) {
    const Pawn* pawn = GetPawn();
    if (!sound || !pawn) {
        return;
    }
    // Close to full volume for whoever holds it, and heard far around.
    GetScene().GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
        sound.GetShared(), pawn->GetViewPoint().position,
        {.volume = volume,
         .bus = "Effects",
         .spatialSettings = {.minDistance = 3.0f, .maxDistance = 100.0f},
         .priority = 5,
         .maxInstances = 4});
}

void Firearm::Fire() {
    const Pawn* pawn = GetPawn();
    const Inventory* inventory = GetInventory();
    if (!pawn || (inventory && !inventory->CanUseItem()) || !TakeShot()) {
        return;
    }
    const Spot::Entity& holder = pawn->GetEntity();
    auto* view = holder.GetComponent<ViewModel>();
    if (view && (!view->IsActiveAndEnabled() || !view->GetShownModel())) {
        view = nullptr;
    }
    auto* world = holder.GetComponent<WorldModel>();
    if (world && world->IsActiveAndEnabled()) {
        world->Synchronize();
    } else {
        world = nullptr;
    }
    // Shots aim from the eyes, where the crosshair is, and are seen leaving the muzzle as it is drawn.
    const ViewPoint eyes = pawn->GetViewPoint();
    const Spot::Vector3 muzzle = view ? view->GetMuzzlePosition() : world ? world->GetMuzzlePosition() : eyes.position;
    if (view) {
        view->Kick(viewRecoil);
    }
    // Only a player's own shots shake their gamepad.
    if (pawn->IsPossessed()) {
        Spot::Input::RumbleGamepad(0.2f, projectile ? 0.8f : 0.5f, 0.08f);
    }
    PlaySound(fireSound, 1.0f);
    EffectLibrary::Play(GetScene(), muzzleEffect, muzzle, eyes.direction,
                        {.scale = std::clamp(muzzleFlash, 0.1f, 4.0f)});
    FireShot(eyes.position, eyes.direction, muzzle);
    if (auto* look = holder.GetComponent<FirstPersonView>()) {
        look->Kick(cameraRecoil * (m_aiming ? 0.65f : 1.0f), recoilRecovery);
    }
}

void Firearm::FireShot(Spot::Vector3 eye, Spot::Vector3 aim, Spot::Vector3 muzzle) {
    if (projectile) {
        Launch(eye, aim, muzzle);
    } else {
        FirePellets(eye, aim, muzzle);
    }
}

void Firearm::Launch(Spot::Vector3 eye, Spot::Vector3 aim, Spot::Vector3 muzzle) {
    const Pawn* pawn = GetPawn();
    const Spot::EntityId shooter = pawn ? pawn->GetEntity().GetId() : GetEntity().GetId();
    auto& physics = GetScene().GetOrAddSystem<Spot::PhysicsWorld>();
    // From the muzzle toward what the crosshair is on, unless a wall is closer than the muzzle.
    const float reach = 200.0f;
    const auto target = physics.Raycast(eye, aim, reach, {.ignore = shooter});
    const Spot::Vector3 point = target ? target->point : eye + aim * reach;
    const float toMuzzle = Spot::Distance(muzzle, eye);
    const auto blocked = toMuzzle > 0.01f
                             ? physics.Raycast(eye, (muzzle - eye) / toMuzzle, toMuzzle, {.ignore = shooter})
                             : std::nullopt;
    const bool clear = toMuzzle > 0.01f && !blocked && (!target || target->distance > 1.5f);
    const Spot::Vector3 start = clear ? muzzle : eye + aim * 0.5f;
    const Spot::Vector3 direction = clear ? Spot::Normalize(point - start) : aim;
    Projectile::Launch(GetScene(), projectile, start, direction, projectileSpeed, shooter);
}

void Firearm::FirePellets(Spot::Vector3 eye, Spot::Vector3 aim, Spot::Vector3 muzzle) {
    const Pawn* pawn = GetPawn();
    const Spot::EntityId shooter = pawn ? pawn->GetEntity().GetId() : GetEntity().GetId();
    const float cone = m_aiming ? spread * aimSpread : spread;
    for (int pellet = 0; pellet < pellets; ++pellet) {
        const Spot::Vector3 direction = Scatter(aim, cone);
        const auto hit =
            GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(eye, direction, range, {.ignore = shooter});
        const auto volume = Traces::Raycast(GetScene(), eye, direction, hit ? hit->distance : range);
        // A streak of light from the muzzle to where the pellet went; every other one of a spread is enough.
        if (pellet % 2 == 0) {
            EffectLibrary::PlayTo(GetScene(), tracerEffect, muzzle,
                                  volume ? volume->point
                                  : hit  ? hit->point
                                         : eye + direction * range);
        }
        // Volumes such as water stop shots soon: what is deeper is out of reach, and what is close below gets less
        // of a push.
        float push = 1.0f;
        if (volume) {
            volume->volume->OnShot(volume->point, 0.25f / std::sqrt(static_cast<float>(std::max(pellets, 1))));
            const float depth = volume->volume->GetShotDepth();
            const float under = hit ? hit->distance - volume->distance : range;
            if (under > depth) {
                continue;
            }
            push = std::isfinite(depth) ? 0.3f * (1.0f - under / std::max(depth, 0.01f)) : 1.0f;
        }
        if (!hit) {
            continue;
        }
        Spot::Entity* entity = GetScene().GetEntity(hit->entity);
        if (!entity) {
            continue;
        }
        if (!volume) {
            if (const auto* surface = SurfaceLibrary::Of(GetScene(), entity)) {
                EffectLibrary::Play(GetScene(), surface->impactEffect, hit->point, hit->normal);
            }
        }
        Damage::Apply(GetScene(), hit->entity,
                      {.amount = damage * push,
                       .type = damageType,
                       .point = hit->point,
                       .impulse = direction * impulse * push,
                       .instigator = shooter,
                       .source = GetEntity().GetId()});
    }
}

Spot::Vector3 Firearm::Scatter(Spot::Vector3 aim, float cone) {
    if (cone <= 0.0f) {
        return aim;
    }
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    // Even over the cone's disc: the square root keeps pellets from bunching at the middle.
    const float radius = std::tan(cone) * std::sqrt(unit(m_random));
    const float angle = 2.0f * Spot::Pi * unit(m_random);
    const Spot::Vector3 side = Spot::Normalize(
        Spot::Cross(aim, std::abs(aim.y) < 0.99f ? Spot::Vector3{0.0f, 1.0f, 0.0f} : Spot::Vector3{1.0f, 0.0f, 0.0f}));
    const Spot::Vector3 up = Spot::Cross(side, aim);
    return Spot::Normalize(aim + (side * std::cos(angle) + up * std::sin(angle)) * radius);
}

void Firearm::OnUse(const ItemInput& input) {
    // Both hands are busy reloading; a carried prop, a placement or the wheel take the aim and the fire actions.
    SetAiming(input.active && input.IsDown(aimAction) && !IsReloading());
    if (input.active && input.IsPressed(reloadAction)) {
        BeginReload();
    }
    const bool firing = automatic ? input.IsDown(fireAction) : input.IsPressed(fireAction);
    if (firing && input.canUse) {
        Fire();
    }
    // Out of rounds, it reloads by itself.
    if (m_ammo == 0) {
        BeginReload();
    }
}

void Firearm::OnUnequip() {
    SetAiming(false);
}

void Firearm::OnDisable() {
    SetAiming(false);
}

ItemStatus Firearm::DescribeStatus() const {
    ItemStatus status = HeldItem::DescribeStatus();
    status.count = std::to_string(m_ammo);
    status.detail = std::format("/ {}", m_reserve);
    status.summary = std::format("{} / {}", m_ammo, m_reserve);
    status.warning = magazineSize > 3 && m_ammo <= magazineSize / 4 && !IsReloading();
    status.hints = {{reloadAction, "Reload"}, {aimAction, "Aim"}};
    if (IsReloading()) {
        status.progress = GetReloadProgress();
        status.hints = {{"", "Reloading"}};
        status.urgent = true;
    } else if (m_ammo == 0 && m_reserve == 0) {
        status.hints = {{"", "Out of ammunition"}};
        status.urgent = true;
    } else if (status.warning) {
        status.urgent = true;
    }
    return status;
}

ItemPose Firearm::DescribePose() const {
    return {.reloading = IsReloading(), .reloadProgress = GetReloadProgress(), .empty = m_ammo == 0};
}

void Firearm::OnStart() {
    GetInput().SetActions({fireAction, aimAction, reloadAction});
    const int capacity = std::max(0, magazineSize);
    m_ammo = m_ammo < 0 ? capacity : std::clamp(m_ammo, 0, capacity);
    m_reserve = std::max(0, m_reserve < 0 ? reserveAmmo : m_reserve);
    if (m_savedCooldown > 0.0f) {
        m_cooldown = Invoke(m_savedCooldown, [] {});
    }
    if (m_savedReloadPending) {
        ScheduleReload(m_savedReload);
    }
}

void Firearm::Serialize(Spot::Archive& archive) {
    HeldItem::Serialize(archive);
    archive.Field("cameraRecoil", cameraRecoil,
                  {.min = 0, .max = 0.2f, .speed = 0.001f, .tooltip = "Upward kick in radians"});
    archive.Field("viewRecoil", viewRecoil, {.min = 0, .max = 0.25f, .speed = 0.005f});
    archive.Field("recoilRecovery", recoilRecovery, {.min = 0.01f, .max = 2, .speed = 0.01f});
    archive.Field("muzzleFlash", muzzleFlash,
                  {.min = 0.1f, .max = 4.0f, .speed = 0.01f, .tooltip = "Size of the flash and reach of its light"});
    archive.Field("automatic", automatic, {.tooltip = "Fires again while the action is held"});
    archive.Field("fireInterval", fireInterval, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds"});
    archive.Field("pellets", pellets, {.min = 1.0f, .max = 64.0f});
    archive.Field("spread", spread, {.min = 0.0f, .max = 0.5f, .angle = true, .tooltip = "Half angle of the cone"});
    archive.Field("range", range, {.min = 0.0f, .max = 10000.0f, .speed = 1.0f});
    archive.Field("damage", damage, {.min = 0.0f, .max = 10000.0f, .tooltip = "Health a pellet's hit takes"});
    archive.Field("impulse", impulse,
                  {.min = 0.0f, .max = 10000.0f, .speed = 0.5f, .tooltip = "Per pellet, in newton seconds"});
    archive.Field("damageType", damageType, {.tooltip = "What its hits are to what they hit"});
    archive.Field("magazineSize", magazineSize, {.min = 1.0f, .max = 1000.0f});
    archive.Field("reserveAmmo", reserveAmmo, {.min = 0.0f, .max = 10000.0f});
    archive.Field("reloadTime", reloadTime, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds"});
    archive.Field("aimZoom", aimZoom,
                  {.min = 1.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "How many times aiming magnifies the view"});
    archive.Field("fireSound", fireSound);
    archive.Field("reloadSound", reloadSound);
    archive.Field("aimSpread", aimSpread,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "The share of the spread left while aiming"});
    archive.Field("muzzleEffect", muzzleEffect);
    archive.Field("tracerEffect", tracerEffect);
    archive.Field("projectile", projectile, {.tooltip = "Launched instead of hitting at once, as a rocket"});
    archive.Field("projectileSpeed", projectileSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
    archive.Field("fireAction", fireAction);
    archive.Field("aimAction", aimAction);
    archive.Field("reloadAction", reloadAction);
    if (archive.IsSaving()) {
        m_savedCooldown = GetInvokeTimeLeft(m_cooldown);
        m_savedReload = GetInvokeTimeLeft(m_reload);
        m_savedReloadPending = IsReloading();
    }
    archive.Field("ammo", m_ammo, {.hidden = true});
    archive.Field("reserve", m_reserve, {.hidden = true});
    archive.Field("cooldownLeft", m_savedCooldown, {.hidden = true});
    archive.Field("reloadLeft", m_savedReload, {.hidden = true});
    archive.Field("reloadPending", m_savedReloadPending, {.hidden = true});
}

} // namespace Gameplay
