#pragma once

#include <spot.h>

#include <string>

// A weapon described as data, on an entity of its own below the player, which a WeaponHolder fires. Hitscan weapons
// hit at once along their pellets' rays, pushing what they hit at the point; projectile weapons fire rockets that
// explode where they land.
class Weapon final : public Spot::Component {
public:
    enum class Kind { Hitscan, Projectile };

    std::string displayName = "Weapon";
    Kind kind = Kind::Hitscan;
    // Fires again while the button is held.
    bool automatic = false;
    // Seconds between shots.
    float fireInterval = 0.25f;
    // Rays per shot, spread within a cone of this half angle, in radians.
    int pellets = 1;
    float spread = 0.0f;
    float range = 100.0f;
    // What a pellet's hit gives a body, in newton seconds.
    float impulse = 30.0f;
    int magazineSize = 12;
    int reserveAmmo = 60;
    // Seconds.
    float reloadTime = 1.2f;
    // Projectiles only.
    float projectileSpeed = 30.0f;
    float explosionRadius = 5.0f;
    float explosionImpulse = 400.0f;

    int GetAmmo() const { return m_ammo; }
    int GetReserve() const { return m_reserve; }
    bool IsReloading() const { return m_reloading > 0.0f; }
    // Whether it can fire now: loaded, not reloading and past its interval.
    bool IsReady() const;
    // Takes a round, starting the interval; false when it is not ready.
    bool TakeShot();
    void StartReload();
    // Counts down the interval and the reload.
    void Tick(float deltaTime);

protected:
    void OnStart() override;
    void Serialize(Spot::Archive& archive) override;

private:
    int m_ammo = 0;
    int m_reserve = 0;
    float m_cooldown = 0.0f;
    float m_reloading = 0.0f;
};
