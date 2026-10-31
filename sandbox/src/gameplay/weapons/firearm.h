#pragma once

#include "gameplay/inventory/held_item.h"

#include <spot.h>

#include <random>
#include <string>
#include <vector>

namespace Gameplay {

// A weapon described as data, an item in hand: the fire action fires it from its pawn's eyes where they look, holding
// it fires automatic ones, the aim action aims down its sights, magnifying the view, and the reload action reloads it,
// as it does by itself once empty. Without a projectile, its pellets hit at once along their rays (Traces), damaging
// and pushing what they hit (Damage::Apply()) and playing the effect of its surface; with one, it launches that prefab,
// as a rocket. While aiming its pawn is "Aiming".
//
// On the machine of a player whose pawn is predicted, it aims, kicks, flashes and sounds at once (OnPredictedUse()),
// and shows where its pellets go; the server alone damages, pushes and launches, and the rounds are its to count.
// That machine tells the server which replicated objects its pellets hit, as it saw them, a little in the past: the
// server damages those, as many as the shots it fired allow and within range, rather than what its own rays hit.
//
// A game changes how it shoots by overriding FireShot(), as a weapon that heals what it hits would; it runs on that
// player's machine too, where it only shows (Spot::Network::HasAuthority() is false there).
class Firearm : public HeldItem, public Spot::Replicated {
public:
    // Fires again while the action is held.
    bool automatic = false;
    // Seconds between shots.
    float fireInterval = 0.25f;
    // Rays per shot, spread within a cone of this half angle, in radians.
    int pellets = 1;
    float spread = 0.0f;
    float range = 100.0f;
    // The health a pellet's hit takes.
    float damage = 20.0f;
    // What a pellet's hit gives a body, in newton seconds.
    float impulse = 30.0f;
    // What its hits are to what they hit (see DamageInfo).
    std::string damageType = "Bullet";
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
    // How big its muzzle flash is, and how far its light reaches.
    float muzzleFlash = 1.0f;
    Spot::AssetRef<Spot::Sound> fireSound;
    Spot::AssetRef<Spot::Sound> reloadSound;
    // The effects of the EffectLibrary its shots play: at the muzzle, and the streak each pellet leaves.
    std::string muzzleEffect = "muzzle";
    std::string tracerEffect = "tracer";
    // What it launches instead of hitting at once, such as a rocket with a Projectile, and how fast.
    Spot::AssetRef<Spot::Prefab> projectile;
    float projectileSpeed = 30.0f;
    std::string fireAction = "Fire";
    std::string aimAction = "Aim";
    std::string reloadAction = "Reload";

    Firearm();

    int GetAmmo() const { return m_ammo; }
    int GetReserve() const { return m_reserve; }
    bool IsReloading() const { return IsInvokePending(m_reload); }
    // How far the reload has gone, from 0 as it starts to 1 as the rounds go in; 0 when not reloading.
    float GetReloadProgress() const;
    // Whether it can fire now: loaded, not reloading and past its interval.
    bool IsReady() const;
    bool IsAiming() const { return m_aiming; }
    // Takes a round, starting the interval; false when it is not ready.
    bool TakeShot();
    // Fires from its pawn's eyes, where they look, when it is ready and its inventory can use it.
    void Fire();
    // Returns whether it started now.
    bool StartReload();
    // Restores authored ammunition and cancels the current cooldown/reload.
    void Refill();

    float GetZoom() const override;
    ItemStatus DescribeStatus() const override;
    ItemPose DescribePose() const override;

    // Its rounds, aim and reload, which the server decides, for the other machines to show.
    void Replicate(Spot::Archive& archive) override;

    // A pellet's hit on a replicated object, as the machine of a player predicting its pawn saw it.
    struct ReportedHit {
        Spot::EntityId target = Spot::EntityId::Invalid;
        Spot::Vector3 point;
        Spot::Vector3 direction;
        // The share of the damage and push left, as water slows pellets.
        float share = 1.0f;

        void Serialize(Spot::Archive& archive);
    };
    // On the server: hits that machine reports, applied as the shots it fired here allow.
    void ApplyReportedHits(std::vector<ReportedHit> hits);

protected:
    // One shot, from the eyes along the aim, seen leaving the muzzle: its pellets, or what it launches.
    virtual void FireShot(Spot::Vector3 eye, Spot::Vector3 aim, Spot::Vector3 muzzle);
    void FirePellets(Spot::Vector3 eye, Spot::Vector3 aim, Spot::Vector3 muzzle);
    void Launch(Spot::Vector3 eye, Spot::Vector3 aim, Spot::Vector3 muzzle);

    void OnUse(const ItemInput& input) override;
    void OnPredictedUse(const ItemInput& input) override { OnUse(input); }
    void OnUnequip() override;
    void OnStart() override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Plays a sound of the weapon from its pawn's hands.
    void PlaySound(const Spot::AssetRef<Spot::Sound>& sound, float volume);
    void BeginReload();
    void SetAiming(bool aiming);
    // A direction within the cone of the half angle around the aim.
    Spot::Vector3 Scatter(Spot::Vector3 aim, float cone);
    void ScheduleReload(float seconds);
    // Applies the waiting reported hits the shots fired allow.
    void ApplyPendingHits();

    std::vector<ReportedHit> m_pendingHits;
    // How many more pellets may be reported hitting, from shots fired here.
    float m_hitAllowance = 0.0f;

    int m_ammo = -1;
    int m_reserve = -1;
    float m_savedCooldown = 0.0f;
    float m_savedReload = 0.0f;
    bool m_savedReloadPending = false;
    bool m_aiming = false;
    Spot::InvokeId m_cooldown = Spot::InvokeId::Invalid;
    Spot::InvokeId m_reload = Spot::InvokeId::Invalid;
    // Fixed, so a game's shots scatter the same way every time it runs.
    std::mt19937 m_random{1234};
};

} // namespace Gameplay
