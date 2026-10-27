#pragma once

#include "gameplay/hud/hud.h"

#include <spot.h>

namespace Gameplay {

class Inventory;

// What is in hand, as its item describes it (HeldItem::DescribeStatus()): its icon, its name a moment after
// switching, a large line, a smaller one beside it, the progress bar and the controls that matter for it, shown after
// switching, early on, and while they matter, such as while reloading.
class ItemPanelElement : public HudElement {
public:
    Spot::EntityId name = Spot::EntityId::Invalid;
    Spot::EntityId icon = Spot::EntityId::Invalid;
    Spot::EntityId count = Spot::EntityId::Invalid;
    Spot::EntityId detail = Spot::EntityId::Invalid;
    Spot::EntityId hints = Spot::EntityId::Invalid;
    Spot::EntityId reloadTrack = Spot::EntityId::Invalid;
    Spot::EntityId reloadFill = Spot::EntityId::Invalid;
    // The progress bar's width when full, in canvas pixels.
    float reloadWidth = 264.0f;
    // Seconds the name of what is in hand shows after switching, before it fades.
    float nameTime = 2.5f;
    Spot::Color accent = {1.0f, 0.68f, 0.22f, 1.0f};
    Spot::Color textColor = {0.8070f, 0.8228f, 0.8469f, 1.0f};
    Spot::Color warningColor = {0.7835f, 0.0865f, 0.0704f, 1.0f};
    // Of the large line, as a number and as a word.
    float countSize = 28.0f;
    float wordSize = 15.0f;

    ItemPanelElement() { hideWhen = {"InVehicle"}; }

    static Inventory* GetInventory(const HudContext& context);

protected:
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::EntityId m_lastItem = Spot::EntityId::Invalid;
    bool m_started = false;
    float m_nameLeft = 0.0f;
};

// The weapon wheel while it is open, filled with the Inventory's items.
class WheelElement : public HudElement {
public:
    Spot::EntityId wheel = Spot::EntityId::Invalid;

protected:
    bool IsWanted(const HudContext& context) override;
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
