#pragma once

#include "gameplay/health/damage.h"

#include <spot.h>

namespace Gameplay {

// Connects health to a physical corpse, including when health arrives from the server.
class CharacterDeath : public Spot::Component, public DamageReceiver {
public:
    Spot::ComponentRef<Spot::Ragdoll> ragdoll;

protected:
    void OnStart() override;
    void OnDamage(const DamageInfo& damage) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void SetDead(bool dead);
    Spot::SignalConnection m_died;
    Spot::SignalConnection m_restored;
    bool m_characterWasEnabled = false;
};

} // namespace Gameplay
