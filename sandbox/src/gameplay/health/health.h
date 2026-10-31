#pragma once

#include "gameplay/health/damage.h"

#include <spot.h>

namespace Gameplay {

// Health that damage takes away and that comes back on its own a few seconds after the last hurt. At none its pawn
// is "Dead", which parts of it read to stop. It stays dead until the game restores it with Restore().
//
// A game changes how damage hurts by overriding AdjustDamage(), as armor would, or replaces Health with a receiver of
// its own; nothing else knows it. In a session, the server decides it: damage hurts only there, and the other machines
// are shown its health, its hurts and its death.
class Health : public Spot::Component, public DamageReceiver, public Spot::Replicated {
public:
    float maxHealth = 100.0f;
    // Seconds after the last hurt before health comes back, and how much it comes back each second then.
    float regenerationDelay = 5.0f;
    float regenerationRate = 12.0f;

    float GetHealth() const { return m_health; }
    bool IsDead() const { return m_health <= 0.0f; }
    // From 1 as it is hurt, fading to 0 within a second, for the HUD to flash.
    float GetHurt() const { return m_hurt; }
    // Takes the damage, adjusted, dying at none; returns how much was taken, nothing once dead.
    float TakeDamage(const DamageInfo& damage);
    // Brings it back with full health.
    void Restore();

    // The damage taken and how much it took.
    Spot::Signal<const DamageInfo&, float> OnDamaged;
    // The damage that killed it.
    Spot::Signal<const DamageInfo&> OnDied;
    Spot::Signal<> OnRestored;

    void Replicate(Spot::Archive& archive) override;

protected:
    // How much health the damage takes, before what is left limits it.
    virtual float AdjustDamage(const DamageInfo& damage) const { return damage.amount; }

    void OnDamage(const DamageInfo& damage) override { TakeDamage(damage); }
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Die(const DamageInfo& damage);
    void SetDead(bool dead);

    float m_health = 100.0f;
    float m_sinceHurt = 100.0f;
    float m_hurt = 0.0f;
};

} // namespace Gameplay
