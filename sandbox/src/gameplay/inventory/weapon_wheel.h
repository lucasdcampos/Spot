#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

// The weapon wheel the HUD shows while an Inventory's wheel is open: its items on the sectors of a ring, clockwise
// from the top, each with its icon and number key, the one pointed at lit, and the name and summary of that one in the
// middle. The HUD's WheelElement fills it; it only draws, within its rectangle.
class WeaponWheel : public Spot::UiWidget {
public:
    struct Slot {
        std::string name;
        // Its rounds or mode, under the name in the middle.
        std::string detail;
        Spot::AssetRef<Spot::Texture> icon;
    };

    Spot::AssetRef<Spot::Font> font;
    // For the number keys and the rounds; the font when unset.
    Spot::AssetRef<Spot::Font> monoFont;
    // One sector of the ring pointing up, as the generator paints ui_wheel_sector.
    Spot::AssetRef<Spot::Texture> sector;
    // The round middle, as ui_dot.
    Spot::AssetRef<Spot::Texture> disc;
    Spot::Color accent = {1.0f, 0.68f, 0.22f, 1.0f};
    // The sectors, the one in hand, the middle, the text on the accent, and the text.
    Spot::Color sectorColor = {0.0052f, 0.0060f, 0.0075f, 1.0f};
    Spot::Color selectedColor = {0.0168f, 0.0203f, 0.0262f, 1.0f};
    Spot::Color inkColor = {0.0070f, 0.0060f, 0.0052f, 1.0f};
    Spot::Color textColor = {0.8070f, 0.8228f, 0.8469f, 1.0f};
    Spot::Color secondaryColor = {0.3231f, 0.3663f, 0.4233f, 1.0f};
    // How many sectors the ring has, as the Inventory's wheel.
    int sectors = 8;
    std::vector<Slot> slots;
    // The slot pointed at, and the one in hand.
    int highlighted = 0;
    int selected = 0;

protected:
    void DrawWidget(Spot::Renderer2D& renderer, const Spot::UiLayoutEntry& entry) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Draws the text centered on the point, at the size in canvas pixels.
    void DrawCentered(Spot::Renderer2D& renderer, const Spot::AssetRef<Spot::Font>& textFont, std::string_view text,
                      Spot::Vector2 center, float size, Spot::Color color, float scale) const;
};

} // namespace Gameplay
