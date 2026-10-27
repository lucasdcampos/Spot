#pragma once

#include "gameplay/hud/hud.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

// The controls, as the player has them bound, in a panel of the HUD that the toggle action shows and hides: a row for
// each action listed, in two columns, made from a template. Until it is first shown, its hint names the key that shows
// it, as the game starts.
class HelpPanel : public HudElement {
public:
    Spot::EntityId rows = Spot::EntityId::Invalid;
    Spot::EntityId rowTemplate = Spot::EntityId::Invalid;
    // The line that names the key that shows the panel.
    Spot::EntityId hint = Spot::EntityId::Invalid;
    std::string toggleAction = "Help";
    // The actions it lists, in its order; those the action map lacks are left out.
    std::vector<std::string> actions = {
        "Move",       "Jump", "Sprint",   "Crouch",   "Fire",       "Aim",    "Reload", "Use",        "Weapon Wheel",
        "Spawn Menu", "Undo", "Clean Up", "Unfreeze", "Flashlight", "Noclip", "Pause",  "Statistics", "Help"};
    Spot::AssetRef<Spot::Font> font;
    // For the keys; the font when unset.
    Spot::AssetRef<Spot::Font> monoFont;
    float rowHeight = 20.0f;

    bool IsToggled() const { return m_toggled; }

protected:
    bool IsWanted(const HudContext& context) override;
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Build();
    void UpdateLabels();

    struct Row {
        std::string action;
        Spot::EntityId entity;
    };
    std::vector<Row> m_rows;
    bool m_toggled = false;
    bool m_built = false;
};

} // namespace Gameplay
