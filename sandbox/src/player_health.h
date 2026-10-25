#pragma once

#include <spot.h>

// The player's health. Landing faster than a safe speed hurts (PlayerMovement tells it), and so do explosions nearby
// (Explosion::Explode()); health comes back on its own a few seconds after the last hurt. At none the player dies:
// it gets out of the car it drives, what it holds drops, it can no longer move or use anything, and after respawnTime
// it comes back at the start with full health.
class PlayerHealth final : public Spot::Component {
public:
    float maxHealth = 100.0f;
    // Landing faster than this, in meters per second, hurts, more the faster, up to killing at lethalFallSpeed.
    float safeFallSpeed = 12.0f;
    float lethalFallSpeed = 26.0f;
    // Seconds after the last hurt before health comes back, and how much it comes back each second then.
    float regenerationDelay = 5.0f;
    float regenerationRate = 12.0f;
    float respawnTime = 4.0f;

    float GetHealth() const { return m_health; }
    bool IsDead() const { return m_health <= 0.0f; }
    // From 1 as the player is hurt, fading to 0 within a second, for the HUD to flash.
    float GetHurt() const { return m_hurt; }
    // Seconds before the dead player comes back; 0 while alive.
    float GetRespawnTimeLeft() const;
    // Takes the amount away, killing the player at none; returns how much was taken, nothing once dead.
    float Damage(float amount);
    // Hurts as landing at the speed, in meters per second, does.
    float Land(float speed);
    // Brings the player back at the start, with full health.
    void Respawn();

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Die();

    float m_health = 100.0f;
    float m_sinceHurt = 100.0f;
    float m_hurt = 0.0f;
    Spot::InvokeId m_respawn = Spot::InvokeId::Invalid;
};
