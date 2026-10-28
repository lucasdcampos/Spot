#include "gameplay/health/health.h"

#include "gameplay/core/pawn.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(Health);

float Health::TakeDamage(const DamageInfo& damage) {
    const float amount = AdjustDamage(damage);
    if (IsDead() || !(amount > 0.0f)) {
        return 0.0f;
    }
    const float taken = std::min(amount, m_health);
    m_health -= taken;
    m_sinceHurt = 0.0f;
    m_hurt = 1.0f;
    OnDamaged.Emit(damage, taken);
    if (m_health <= 0.0f) {
        Die(damage);
    }
    return taken;
}

void Health::Die(const DamageInfo& damage) {
    m_health = 0.0f;
    SetDead(true);
    OnDied.Emit(damage);
    DamageSystem::Get(GetScene()).OnKilled.Emit({GetEntity().GetId(), damage});
}

void Health::Restore() {
    m_health = maxHealth;
    m_hurt = 0.0f;
    m_sinceHurt = 100.0f;
    SetDead(false);
    OnRestored.Emit();
}

void Health::SetDead(bool dead) {
    if (Pawn* pawn = Pawn::Of(GetEntity())) {
        pawn->SetState("Dead", dead);
    }
}

void Health::OnStart() {
    m_health = maxHealth;
}

void Health::OnUpdate(float deltaTime) {
    m_hurt = std::max(m_hurt - deltaTime, 0.0f);
    if (IsDead()) {
        return;
    }
    m_sinceHurt += deltaTime;
    if (m_sinceHurt >= regenerationDelay) {
        m_health = std::min(m_health + regenerationRate * deltaTime, maxHealth);
    }
}

void Health::Serialize(Spot::Archive& archive) {
    archive.Field("maxHealth", maxHealth, {.min = 1.0f, .max = 10000.0f, .speed = 1.0f});
    archive.Field("regenerationDelay", regenerationDelay,
                  {.min = 0.0f, .max = 60.0f, .speed = 0.1f, .tooltip = "Seconds after a hurt before healing"});
    archive.Field("regenerationRate", regenerationRate,
                  {.min = 0.0f, .max = 1000.0f, .speed = 0.5f, .tooltip = "Health a second while healing"});
}

} // namespace Gameplay
