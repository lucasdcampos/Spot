#include "player_health.h"

#include "physics_gun.h"
#include "player_controller.h"
#include "player_movement.h"
#include "prop_carrier.h"

#include <algorithm>

SPOT_COMPONENT(PlayerHealth);

float PlayerHealth::GetRespawnTimeLeft() const {
    return IsDead() ? GetInvokeTimeLeft(m_respawn) : 0.0f;
}

float PlayerHealth::Damage(float amount) {
    if (IsDead() || !(amount > 0.0f)) {
        return 0.0f;
    }
    const float taken = std::min(amount, m_health);
    m_health -= taken;
    m_sinceHurt = 0.0f;
    m_hurt = 1.0f;
    Spot::Input::RumbleGamepad(0.6f, 0.9f, 0.2f);
    if (auto* player = GetEntity().GetComponent<PlayerController>()) {
        player->Kick(std::min(taken * 0.002f, 0.08f), 0.3f);
    }
    if (m_health <= 0.0f) {
        Die();
    }
    return taken;
}

float PlayerHealth::Land(float speed) {
    if (speed <= safeFallSpeed || lethalFallSpeed <= safeFallSpeed) {
        return 0.0f;
    }
    return Damage(maxHealth * (speed - safeFallSpeed) / (lethalFallSpeed - safeFallSpeed));
}

void PlayerHealth::Die() {
    m_health = 0.0f;
    if (auto* carrier = GetEntity().GetComponent<PropCarrier>()) {
        carrier->Drop();
    }
    if (auto* gun = GetEntity().GetComponent<PhysicsGun>()) {
        gun->Release();
    }
    m_respawn = Invoke(std::max(respawnTime, 0.0f), [this] { Respawn(); });
}

void PlayerHealth::Respawn() {
    CancelInvoke(m_respawn);
    m_respawn = Spot::InvokeId::Invalid;
    m_health = maxHealth;
    m_hurt = 0.0f;
    m_sinceHurt = 100.0f;
    if (auto* movement = GetEntity().GetComponent<PlayerMovement>()) {
        movement->Respawn();
    }
}

void PlayerHealth::OnStart() {
    m_health = maxHealth;
}

void PlayerHealth::OnUpdate(float deltaTime) {
    m_hurt = std::max(m_hurt - deltaTime, 0.0f);
    if (IsDead()) {
        return;
    }
    m_sinceHurt += deltaTime;
    if (m_sinceHurt >= regenerationDelay) {
        m_health = std::min(m_health + regenerationRate * deltaTime, maxHealth);
    }
}

void PlayerHealth::Serialize(Spot::Archive& archive) {
    archive.Field("maxHealth", maxHealth, {.min = 1.0f, .max = 10000.0f, .speed = 1.0f});
    archive.Field("safeFallSpeed", safeFallSpeed,
                  {.min = 0.0f, .max = 100.0f, .speed = 0.1f, .tooltip = "Landing faster, in m/s, hurts"});
    archive.Field("lethalFallSpeed", lethalFallSpeed,
                  {.min = 0.0f, .max = 200.0f, .speed = 0.1f, .tooltip = "Landing this fast, in m/s, kills"});
    archive.Field("regenerationDelay", regenerationDelay,
                  {.min = 0.0f, .max = 60.0f, .speed = 0.1f, .tooltip = "Seconds after a hurt before healing"});
    archive.Field("regenerationRate", regenerationRate,
                  {.min = 0.0f, .max = 1000.0f, .speed = 0.5f, .tooltip = "Health a second while healing"});
    archive.Field("respawnTime", respawnTime, {.min = 0.0f, .max = 60.0f, .speed = 0.1f, .tooltip = "Seconds"});
}
