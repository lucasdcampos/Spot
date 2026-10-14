#include "weapon.h"

#include <algorithm>
#include <array>
#include <string_view>

SPOT_COMPONENT(Weapon);

bool Weapon::IsReady() const {
    return m_ammo > 0 && !IsReloading() && !IsInvokePending(m_cooldown);
}

float Weapon::GetReloadProgress() const {
    if (!IsReloading() || reloadTime <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(1.0f - GetInvokeTimeLeft(m_reload) / reloadTime, 0.0f, 1.0f);
}

bool Weapon::TakeShot() {
    if (!IsReady()) {
        return false;
    }
    --m_ammo;
    m_cooldown = fireInterval > 0.0f ? Invoke(fireInterval, [] {}) : Spot::InvokeId::Invalid;
    return true;
}

bool Weapon::StartReload() {
    if (IsReloading() || m_ammo >= magazineSize || m_reserve <= 0) {
        return false;
    }
    ScheduleReload(reloadTime);
    return true;
}

void Weapon::Refill() {
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

void Weapon::ScheduleReload(float seconds) {
    m_reload = Invoke(std::max(0.0f, seconds), [this] {
        const int loaded = std::min(magazineSize - m_ammo, m_reserve);
        m_ammo += loaded;
        m_reserve -= loaded;
    });
}

void Weapon::OnStart() {
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

void Weapon::Serialize(Spot::Archive& archive) {
    archive.Field("cameraRecoil", cameraRecoil,
                  {.min = 0, .max = 0.2f, .speed = 0.001f, .tooltip = "Upward kick in radians"});
    archive.Field("viewRecoil", viewRecoil, {.min = 0, .max = 0.25f, .speed = 0.005f});
    archive.Field("recoilRecovery", recoilRecovery, {.min = 0.01f, .max = 2, .speed = 0.01f});
    static constexpr std::array<std::string_view, 2> Kinds = {"Hitscan", "Projectile"};
    archive.Field("displayName", displayName);
    archive.Field("kind", kind, {.enumNames = Kinds});
    archive.Field("automatic", automatic, {.tooltip = "Fires again while the button is held"});
    archive.Field("fireInterval", fireInterval, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds"});
    archive.Field("pellets", pellets, {.min = 1.0f, .max = 64.0f});
    archive.Field("spread", spread, {.min = 0.0f, .max = 0.5f, .angle = true, .tooltip = "Half angle of the cone"});
    archive.Field("range", range, {.min = 0.0f, .max = 10000.0f, .speed = 1.0f});
    archive.Field("impulse", impulse,
                  {.min = 0.0f, .max = 10000.0f, .speed = 0.5f, .tooltip = "Per pellet, in newton seconds"});
    archive.Field("magazineSize", magazineSize, {.min = 1.0f, .max = 1000.0f});
    archive.Field("reserveAmmo", reserveAmmo, {.min = 0.0f, .max = 10000.0f});
    archive.Field("reloadTime", reloadTime, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds"});
    archive.Field("aimZoom", aimZoom,
                  {.min = 1.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "How many times aiming magnifies the view"});
    archive.Field("model", model, {.tooltip = "What the player holds while it is in hand"});
    archive.Field("fireSound", fireSound);
    archive.Field("reloadSound", reloadSound);
    archive.Field("aimSpread", aimSpread,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "The share of the spread left while aiming"});
    archive.Field("projectileSpeed", projectileSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
    archive.Field("explosionRadius", explosionRadius, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("explosionImpulse", explosionImpulse, {.min = 0.0f, .max = 100000.0f, .speed = 1.0f});
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
