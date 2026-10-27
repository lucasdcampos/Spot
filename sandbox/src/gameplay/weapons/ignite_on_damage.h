#pragma once

#include "gameplay/health/damage.h"

#include <spot.h>

namespace Gameplay {

// Sets off its entity's Explosive when anything damages it, shot, caught in a blast or run into, after a short fuse so
// blasts spread from one to the next, as barrels do.
class IgniteOnDamage : public Spot::Component, public DamageReceiver {
public:
    // Seconds between being set off and exploding.
    float fuse = 0.15f;

    bool IsLit() const { return m_lit; }
    void Ignite();

protected:
    void OnDamage(const DamageInfo& damage) override;
    void OnStart() override;
    void Serialize(Spot::Archive& archive) override;

private:
    void ScheduleFuse(float seconds);
    void Explode();

    Spot::InvokeId m_fuse = Spot::InvokeId::Invalid;
    float m_savedFuse = -1.0f;
    bool m_lit = false;
};

} // namespace Gameplay
