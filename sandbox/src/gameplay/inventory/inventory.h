#pragma once

#include "gameplay/core/input_context.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/player_input.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

class HeldItem;
class ItemInput;

// What a pawn has in hand: one of its items, the child entities with a HeldItem, in their order. Each of the select
// actions takes the item of its place, and the cycle action (the mouse wheel, or the shoulders) goes through them.
// Holding the wheel action opens a wheel of the items and slows time: the look points at one, and letting go takes
// it; the pawn is "Choosing" meanwhile, and the open wheel takes the look and the item's actions from everything else.
// The item in hand reads its actions while the pawn is in none of the states of stowedBy, as dead or seated, and may be
// used while it is in none of blockedBy either, as running; it magnifies the pawn's view as it says (ViewZoom).
class Inventory : public Spot::Component, public ViewZoom, public PlayerInputListener {
public:
    // How many items the wheel has room for, clockwise from the top.
    int wheelSlots = 8;
    // How much the wheel slows time while it is open.
    float wheelTimeScale = 0.3f;
    std::vector<std::string> selectActions = {"Weapon 1", "Weapon 2", "Weapon 3", "Weapon 4", "Weapon 5",
                                              "Weapon 6", "Weapon 7", "Weapon 8", "Weapon 9"};
    std::string cycleAction = "Switch Weapon";
    std::string wheelAction = "Weapon Wheel";
    std::string lookAction = "Look";
    std::vector<std::string> stowedBy = {"Dead", "Seated"};
    std::vector<std::string> blockedBy = {"Sprinting", "Placing"};
    // Played on the Interface bus as the item in hand changes, and as the wheel opens and points elsewhere.
    Spot::AssetRef<Spot::Sound> clickSound;

    // Its items, in their order.
    std::vector<HeldItem*> GetItems() const;
    int GetItemCount() const;
    int GetSelected() const { return m_selected; }
    // Takes the item of the place, counted around from either end.
    void Select(int slot);
    void Select(const HeldItem& item);
    // The item in hand; null when there is none.
    HeldItem* GetEquipped() const;
    // Makes the prefab an item of its own, at the end, without what it has to be a body in the world, and returns it;
    // null when it has no HeldItem.
    HeldItem* AddFromPrefab(const Spot::AssetRef<Spot::Prefab>& prefab);

    // Whether the item in hand can be used now: not while choosing in the wheel, nor in a state of stowedBy or
    // blockedBy.
    bool CanUseItem() const;
    // Gives the selected item an intent, from any controller or a test. Inventory states still apply.
    void Use(const ItemInput& input);
    bool IsWheelOpen() const { return m_wheelOpen; }
    // The place the wheel points at, which letting go selects.
    int GetWheelSlot() const { return m_wheelSlot; }
    void OpenWheel();
    // Selects the place pointed at when asked to, and gives time back its speed.
    void CloseWheel(bool select);

    float GetViewZoom() const override;

protected:
    void OnPlayerInput(PlayerController& player) override;
    void OnPlayerUnpossess() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Points the wheel where the look moves.
    void UpdateWheel();
    // Unequips what was in hand and equips what is selected now, when they differ.
    void Equip();
    bool IsStowed() const;
    void PlayClick() const;

    InputContext m_input{{.name = "Inventory", .priority = 10}};
    InputContext m_wheel{{.name = "Weapon Wheel", .priority = 20, .actions = {"Look"}}};
    int m_selected = 0;
    Spot::EntityId m_equipped = Spot::EntityId::Invalid;
    bool m_wheelOpen = false;
    int m_wheelSlot = 0;
    // Where looking has moved the wheel's pointer, within the unit circle.
    Spot::Vector2 m_wheelPointer;
    float m_timeScaleBefore = 1.0f;
    float m_timeScaleSet = 1.0f;
    Spot::SignalConnection m_menuOpening;
};

} // namespace Gameplay
