#pragma once

#include <spot.h>

#include <string>
#include <vector>

// The weapon wheel the HUD shows while Weapon Wheel is held: the WeaponHolder's slots on the sectors of a ring,
// clockwise from the top, each with its item's icon and number key, the one pointed at lit, and the name and rounds of
// that one in the middle. RuntimeInterface fills it from the HudState; it only draws, within its rectangle.
class WeaponWheel final : public Spot::UiWidget {
public:
    struct Slot {
        std::string name;
        // Its rounds or mode, under the name in the middle.
        std::string detail;
        Spot::AssetRef<Spot::Texture> icon;
    };

    Spot::AssetRef<Spot::Font> font;
    // One sector of the ring pointing up, as the generator paints ui_wheel_sector.
    Spot::AssetRef<Spot::Texture> sector;
    // The round middle, as ui_dot.
    Spot::AssetRef<Spot::Texture> disc;
    Spot::Color accent = {1.0f, 0.68f, 0.22f, 1.0f};
    std::vector<Slot> slots;
    // The slot pointed at, and the one in hand.
    int highlighted = 0;
    int selected = 0;

protected:
    void DrawWidget(Spot::Renderer2D& renderer, const Spot::UiLayoutEntry& entry) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Draws the text centered on the point, at the size in canvas pixels.
    void DrawCentered(Spot::Renderer2D& renderer, std::string_view text, Spot::Vector2 center, float size,
                      Spot::Color color, float scale) const;
};
