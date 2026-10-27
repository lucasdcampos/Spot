#pragma once

#include "gameplay/core/input_context.h"
#include "gameplay/hud/input_prompts.h"

#include <spot.h>

#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

class Inventory;
class Pawn;

// What the player does with the item in hand this frame, as its inventory read it from the item's actions: whatever
// controls it, a player or a test, it reads the same.
class ItemInput {
public:
    // Whether it is in hand for someone who can work it now, alive and not seated, say.
    bool active = false;
    // Whether it may be used now: not while running or choosing in the wheel.
    bool canUse = false;

    void Set(std::string_view action, bool down, bool pressed = false, bool released = false, float value = 0.0f);
    bool IsDown(std::string_view action) const;
    bool IsPressed(std::string_view action) const;
    bool IsReleased(std::string_view action) const;
    float GetValue(std::string_view action) const;

    // The actions the context takes that reach it now.
    static ItemInput Read(const InputContext& input, bool canUse);

private:
    struct Action {
        std::string name;
        bool down = false;
        bool pressed = false;
        bool released = false;
        float value = 0.0f;
    };
    const Action* Find(std::string_view action) const;

    std::vector<Action> m_actions;
};

// What an item shows of itself on the HUD and in the weapon wheel.
struct ItemStatus {
    std::string name;
    Spot::AssetRef<Spot::Texture> icon;
    // The large line, as the rounds left or the mode, and whether it is a word rather than a number.
    std::string count;
    bool countIsWord = false;
    // The smaller line beside it, as the rounds in reserve or the mode's option.
    std::string detail;
    // What the wheel says under its name.
    std::string summary;
    // From 0 to 1 while something takes time, as reloading; below 0 when nothing does.
    float progress = -1.0f;
    // Whether the large line warns, as with few rounds left.
    bool warning = false;
    // Whether its controls matter now, as while reloading, so they show.
    bool urgent = false;
    std::vector<InputHint> hints;
};

// How an item is held now, for the view model to move its parts.
struct ItemPose {
    bool reloading = false;
    // From 0 as reloading starts to 1 as it ends.
    float reloadProgress = 0.0f;
    // Out of rounds, as a slide locked back.
    bool empty = false;
    // Working, as the physics gun's prongs spin while it holds something.
    bool active = false;
};

// An item that can be in hand, on an entity of its own below an Inventory's: a firearm, a tool. The inventory equips
// it and gives it, each frame, what the player does with it through its actions (OnUse()), so it never reads input
// itself. It says what it shows (DescribeStatus()) and how it is held (DescribePose()), so the HUD and the view model
// know no kind of item; a new kind of item is one subclass.
class HeldItem : public Spot::Component {
public:
    std::string displayName = "Item";
    // What the HUD and the weapon wheel show for it.
    Spot::AssetRef<Spot::Texture> icon;
    // What is held while it is in hand (see ViewModel).
    Spot::AssetRef<Spot::Model> model;

    // The inventory it is in; null when there is none above it.
    Inventory* GetInventory() const;
    // The pawn holding it, which its inventory is on.
    Pawn* GetPawn() const;
    bool IsEquipped() const { return m_equipped; }
    // The actions it reads, which its inventory activates while it is in hand.
    const InputContext& GetInput() const { return m_input; }
    InputContext& GetInput() { return m_input; }

    // How many times it magnifies the view now; 1 for not at all.
    virtual float GetZoom() const { return 1.0f; }
    virtual ItemStatus DescribeStatus() const;
    virtual ItemPose DescribePose() const { return {}; }

protected:
    explicit HeldItem(std::vector<std::string> actions);

    friend class Inventory;

    virtual void OnEquip() {}
    virtual void OnUnequip() {}
    // Each frame while it is in hand, with what the player does with it.
    virtual void OnUse(const ItemInput& /*input*/) {}

    void Serialize(Spot::Archive& archive) override;

private:
    InputContext m_input;
    bool m_equipped = false;
};

} // namespace Gameplay
