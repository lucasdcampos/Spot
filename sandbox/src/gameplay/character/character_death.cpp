#include "gameplay/character/character_death.h"

#include "gameplay/health/health.h"

namespace Gameplay {

SPOT_COMPONENT(CharacterDeath);

void CharacterDeath::SetDead(bool dead) {
    auto* corpse = ragdoll.Get(GetScene());
    if (!corpse || corpse->IsActive() == dead || !corpse->SetActive(dead)) {
        return;
    }
    if (auto* body = GetEntity().GetComponent<Spot::CharacterController>();
        body && &corpse->GetEntity() != &GetEntity()) {
        if (dead) {
            m_characterWasEnabled = body->IsEnabled();
            body->SetEnabled(false);
        } else {
            body->SetEnabled(m_characterWasEnabled);
        }
    }
}

void CharacterDeath::OnStart() {
    if (auto* health = GetEntity().GetComponent<Health>()) {
        m_died = health->OnDied.Connect([this](const DamageInfo&) { SetDead(true); });
        m_restored = health->OnRestored.Connect([this] { SetDead(false); });
        SetDead(health->IsDead());
    }
}

void CharacterDeath::OnDamage(const DamageInfo& damage) {
    if (auto* corpse = ragdoll.Get(GetScene()); corpse && corpse->IsActive()) {
        corpse->AddImpulse(damage.impulse, damage.point);
    }
}

void CharacterDeath::Serialize(Spot::Archive& archive) {
    archive.Field("ragdoll", ragdoll);
}

} // namespace Gameplay
