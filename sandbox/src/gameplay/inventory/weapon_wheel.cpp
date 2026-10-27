#include "gameplay/inventory/weapon_wheel.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(WeaponWheel);

namespace {

// How the generator paints the sector, in its pixels: the ring's outer and inner radii, and how far above the wheel's
// center the middle of the image is.
constexpr float SectorOuter = 252.0f;
constexpr float SectorInner = 118.0f;
constexpr float SectorImageCenter = 132.0f;
constexpr float SectorImageSize = 256.0f;

} // namespace

void WeaponWheel::DrawCentered(Spot::Renderer2D& renderer, const Spot::AssetRef<Spot::Font>& textFont,
                               std::string_view text, Spot::Vector2 center, float size, Spot::Color color,
                               float scale) const {
    if (!textFont || text.empty()) {
        return;
    }
    const float factor = size / textFont->GetPixelSize() * scale;
    const Spot::Vector2 measured = textFont->MeasureText(text) * factor;
    renderer.DrawString(*textFont, text,
                        {std::round(center.x - measured.x * 0.5f), std::round(center.y + measured.y * 0.5f)}, color,
                        factor);
}

void WeaponWheel::DrawWidget(Spot::Renderer2D& renderer, const Spot::UiLayoutEntry& entry) {
    const Spot::Vector2 center = entry.bounds.GetCenter();
    const float radius = std::min(entry.bounds.GetSize().x, entry.bounds.GetSize().y) * 0.5f;
    if (radius <= 0.0f) {
        return;
    }
    const float pixel = radius / SectorOuter;
    const int count = static_cast<int>(slots.size());
    const Spot::Color dark = sectorColor;
    const Spot::AssetRef<Spot::Font>& numbers = monoFont ? monoFont : font;
    for (int index = 0; index < sectors; ++index) {
        // Clockwise from the top; the renderer turns counterclockwise.
        const float angle = 2.0f * Spot::Pi * static_cast<float>(index) / static_cast<float>(std::max(sectors, 1));
        const Spot::Vector2 out{std::sin(angle), std::cos(angle)};
        const bool used = index < count;
        const bool lit = used && index == highlighted;
        if (sector) {
            const Spot::Color tint = lit     ? accent
                                     : !used ? Spot::Color{dark.r, dark.g, dark.b, 0.3f}
                                     : index == selected
                                         ? Spot::Color{selectedColor.r, selectedColor.g, selectedColor.b, 0.9f}
                                         : Spot::Color{dark.r, dark.g, dark.b, 0.74f};
            renderer.DrawRotatedQuad(center + out * (SectorImageCenter * pixel),
                                     {SectorImageSize * pixel, SectorImageSize * pixel}, -angle, *sector, tint);
        }
        if (!used) {
            continue;
        }
        const Slot& slot = slots[static_cast<std::size_t>(index)];
        const Spot::Color ink = lit ? inkColor : Spot::Color{textColor.r, textColor.g, textColor.b, 0.92f};
        const Spot::Vector2 middle = center + out * ((SectorOuter + SectorInner) * 0.5f * pixel);
        if (slot.icon) {
            // As wide as the sector allows at its middle, keeping the icon's proportions.
            const float width = 112.0f * pixel * (lit ? 1.08f : 1.0f);
            const float height = width * static_cast<float>(slot.icon->GetHeight()) /
                                 static_cast<float>(std::max(slot.icon->GetWidth(), 1u));
            renderer.DrawQuad(middle, {width, height}, *slot.icon, ink);
        }
        DrawCentered(renderer, numbers, std::to_string(index + 1), center + out * ((SectorOuter - 22.0f) * pixel),
                     10.0f, {ink.r, ink.g, ink.b, lit ? 0.8f : 0.5f}, entry.scale);
    }

    if (disc) {
        const float size = SectorInner * 2.0f * pixel * 64.0f / 62.0f - 10.0f * pixel;
        renderer.DrawQuad(center, {size, size}, *disc, {dark.r, dark.g, dark.b, 0.82f});
    }
    if (highlighted >= 0 && highlighted < count) {
        const Slot& slot = slots[static_cast<std::size_t>(highlighted)];
        DrawCentered(renderer, font, slot.name, center + Spot::Vector2{0.0f, 10.0f * entry.scale}, 14.0f, accent,
                     entry.scale);
        DrawCentered(renderer, numbers, slot.detail, center - Spot::Vector2{0.0f, 12.0f * entry.scale}, 12.0f,
                     secondaryColor, entry.scale);
    }
}

void WeaponWheel::Serialize(Spot::Archive& archive) {
    SerializeWidget(archive);
    archive.Field("font", font);
    archive.Field("monoFont", monoFont, {.tooltip = "For the number keys and the rounds"});
    archive.Field("sector", sector, {.tooltip = "One sector of the ring pointing up, as ui_wheel_sector"});
    archive.Field("disc", disc, {.tooltip = "The round middle"});
    archive.Field("accent", accent);
    archive.Field("sectorColor", sectorColor);
    archive.Field("selectedColor", selectedColor, {.tooltip = "The sector of the item in hand"});
    archive.Field("inkColor", inkColor, {.tooltip = "Text on the accent"});
    archive.Field("textColor", textColor);
    archive.Field("secondaryColor", secondaryColor);
    archive.Field("sectors", sectors, {.min = 1.0f, .max = 16.0f});
}

} // namespace Gameplay
