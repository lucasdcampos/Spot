#pragma once

#include <spot.h>

#include <random>

class Weapon;

// What the player has in hand: the physics gun (PhysicsGun), on 1, or one of the weapons on the entities below the
// player, on the keys after it in their order; the wheel goes through them when the physics gun is not holding
// anything. A click fires the weapon in hand, holding it fires automatic ones, and R reloads.
class WeaponHolder final : public Spot::Component {
public:
    // 0 is the physics gun; the weapons follow.
    int GetSelected() const { return m_selected; }
    void Select(int slot);
    bool IsPhysicsGunSelected() const { return m_selected == 0; }
    // The weapon in hand; null with the physics gun.
    Weapon* GetWeapon() const;
    // Every weapon, in the order of their keys.
    std::vector<Weapon*> GetWeapons() const;

    // Fires the weapon in hand from the player's eyes, where it looks, when it is ready.
    void Fire();

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void FireHitscan(const Weapon& weapon, Spot::Vector3 eye, Spot::Vector3 aim);
    // A direction within the cone of the half angle around the aim.
    Spot::Vector3 Scatter(Spot::Vector3 aim, float spread);

    int m_selected = 0;
    // Fixed, so a game's shots scatter the same way every time it runs.
    std::mt19937 m_random{1234};
};
