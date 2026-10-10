#include "weapon.h"

#include <algorithm>
#include <array>
#include <string_view>

SPOT_COMPONENT(Weapon);

bool Weapon::IsReady() const {
    return m_ammo > 0 && m_reloading <= 0.0f && m_cooldown <= 0.0f;
}

bool Weapon::TakeShot() {
    if (!IsReady()) {
        return false;
    }
    --m_ammo;
    m_cooldown = fireInterval;
    return true;
}

void Weapon::StartReload() {
    if (m_reloading > 0.0f || m_ammo >= magazineSize || m_reserve <= 0) {
        return;
    }
    m_reloading = reloadTime;
}

void Weapon::Tick(float deltaTime) {
    m_cooldown = std::max(m_cooldown - deltaTime, 0.0f);
    if (m_reloading > 0.0f) {
        m_reloading -= deltaTime;
        if (m_reloading <= 0.0f) {
            const int loaded = std::min(magazineSize - m_ammo, m_reserve);
            m_ammo += loaded;
            m_reserve -= loaded;
            m_reloading = 0.0f;
        }
    }
}

void Weapon::OnStart() {
    m_ammo = magazineSize;
    m_reserve = reserveAmmo;
}

void Weapon::Serialize(Spot::Archive& archive) {
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
    archive.Field("aimSpread", aimSpread,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "The share of the spread left while aiming"});
    archive.Field("projectileSpeed", projectileSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
    archive.Field("explosionRadius", explosionRadius, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("explosionImpulse", explosionImpulse, {.min = 0.0f, .max = 100000.0f, .speed = 1.0f});
}
