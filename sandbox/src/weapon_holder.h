#pragma once

#include <spot.h>

#include <random>

class Weapon;

// What the player has in hand: the physics gun (PhysicsGun), on Weapon 1, one of the weapons on the entities below the
// player, on the slots after it in their order, or the tool gun (ToolGun), on the slot after them; Switch Weapon (the
// mouse wheel, or the shoulders) goes through them when the physics gun is not holding anything. Holding Weapon Wheel
// opens a wheel of the slots and slows time: looking points at one, and letting go takes it. Fire fires the weapon in
// hand, holding it fires automatic ones, Aim aims down its sights and Reload reloads; nothing in hand is used while
// running, choosing in the wheel or dead.
class WeaponHolder final : public Spot::Component {
public:
    // How many slots the wheel has room for, clockwise from the top.
    static constexpr int WheelSlots = 8;
    // How much the wheel slows time while it is open.
    float wheelTimeScale = 0.3f;

    // 0 is the physics gun; the weapons follow, and the tool gun comes last.
    int GetSelected() const { return m_selected; }
    void Select(int slot);
    bool IsPhysicsGunSelected() const { return m_selected == 0; }
    bool IsToolGunSelected() const { return m_selected == GetWeaponCount() + 1; }
    // The weapon in hand; null with the physics gun.
    Weapon* GetWeapon() const;
    // Every weapon, in the order of their keys.
    std::vector<Weapon*> GetWeapons() const;
    int GetWeaponCount() const;
    // Every slot: the physics gun, the weapons and the tool gun.
    int GetSlotCount() const { return GetWeaponCount() + 2; }

    // Whether the item in hand can be used now: not while running, choosing in the wheel or dead.
    bool CanUseItem() const;
    bool IsWheelOpen() const { return m_wheelOpen; }
    // The slot the wheel points at, which letting go selects.
    int GetWheelSlot() const { return m_wheelSlot; }
    void OpenWheel();
    // Selects the slot pointed at when asked to, and gives time back its speed.
    void CloseWheel(bool select);

    // Fires the weapon in hand from the player's eyes, where it looks, when it is ready.
    void Fire();
    // Whether the player aims down the sights of the weapon in hand.
    bool IsAiming() const { return m_aiming; }
    // How many times aiming magnifies the view now; 1 when not aiming.
    float GetAimZoom() const;
    // From 1 as a shot hits something that reacts, a prop or a robot, fading to 0, for the HUD's hit marker.
    float GetHitMarker() const { return m_hitMarker; }

protected:
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    void FireHitscan(const Weapon& weapon, Spot::Vector3 eye, Spot::Vector3 aim, Spot::Vector3 muzzle);
    // Plays a sound of the weapon from the player's hands.
    void PlayWeaponSound(const Spot::AssetRef<Spot::Sound>& sound, float volume);
    void StartReload(Weapon& weapon);
    // A direction within the cone of the half angle around the aim.
    Spot::Vector3 Scatter(Spot::Vector3 aim, float spread);
    // Points the wheel where the look moves.
    void UpdateWheel();

    int m_selected = 0;
    bool m_aiming = false;
    float m_hitMarker = 0.0f;
    bool m_wheelOpen = false;
    int m_wheelSlot = 0;
    // Where looking has moved the wheel's pointer, within the unit circle.
    Spot::Vector2 m_wheelPointer;
    float m_timeScaleBefore = 1.0f;
    float m_timeScaleSet = 1.0f;
    // Fixed, so a game's shots scatter the same way every time it runs.
    std::mt19937 m_random{1234};
};
