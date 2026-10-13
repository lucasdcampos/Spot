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
    // How many times aiming down the sights magnifies the view, and what share of the spread is left meanwhile.
    float aimZoom = 1.5f;
    float aimSpread = 0.4f;
    float cameraRecoil = 0.025f;
    float viewRecoil = 0.05f;
    float recoilRecovery = 0.2f;
    // What the player holds while it is in hand (see ViewModel).
    Spot::AssetRef<Spot::Model> model;
    Spot::AssetRef<Spot::Sound> fireSound;
    Spot::AssetRef<Spot::Sound> reloadSound;
    // Projectiles only.
    float projectileSpeed = 30.0f;
    float explosionRadius = 5.0f;
    float explosionImpulse = 400.0f;

    int GetAmmo() const { return m_ammo; }
    int GetReserve() const { return m_reserve; }
    bool IsReloading() const { return IsInvokePending(m_reload); }
    // Whether it can fire now: loaded, not reloading and past its interval.
    bool IsReady() const;
    // Takes a round, starting the interval; false when it is not ready.
    bool TakeShot();
    // Returns whether it started now.
    bool StartReload();
    // Restores authored ammunition and cancels the current cooldown/reload.
    void Refill();

protected:
    void OnStart() override;
    void Serialize(Spot::Archive& archive) override;

private:
    int m_ammo = -1;
    int m_reserve = -1;
    float m_savedCooldown = 0.0f;
    float m_savedReload = 0.0f;
    bool m_savedReloadPending = false;
    void ScheduleReload(float seconds);
    Spot::InvokeId m_cooldown = Spot::InvokeId::Invalid;
    Spot::InvokeId m_reload = Spot::InvokeId::Invalid;
};
