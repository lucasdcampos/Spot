#pragma once

#include <spot.h>

// The colors of the sandbox's interface, linear, as the authored interface (prefabs/interface.prefab) uses them:
// graphite surfaces and slate text in three strengths; the amber accent is the menus' style.accent.
namespace InterfacePalette {

inline constexpr Spot::Color Text{0.8070f, 0.8228f, 0.8469f, 1.0f};
inline constexpr Spot::Color TextSecondary{0.3231f, 0.3663f, 0.4233f, 1.0f};
inline constexpr Spot::Color TextMuted{0.1274f, 0.1500f, 0.1845f, 1.0f};
inline constexpr Spot::Color Warning{0.7835f, 0.0865f, 0.0704f, 1.0f};
// Text on the accent.
inline constexpr Spot::Color Ink{0.0070f, 0.0060f, 0.0052f, 1.0f};
inline constexpr Spot::Color Graphite{0.0052f, 0.0060f, 0.0075f, 1.0f};
inline constexpr Spot::Color GraphiteRaised{0.0168f, 0.0203f, 0.0262f, 1.0f};
inline constexpr Spot::Color Surface{0.0091f, 0.0110f, 0.0137f, 1.0f};
inline constexpr Spot::Color SurfaceHover{0.0144f, 0.0176f, 0.0232f, 1.0f};
// The 1 pixel outlines of panels and controls, at rest and under the pointer.
inline constexpr Spot::Color Line{1.0f, 1.0f, 1.0f, 0.07f};
inline constexpr Spot::Color LineHovered{1.0f, 1.0f, 1.0f, 0.16f};
inline constexpr Spot::Color LineFocused{1.0f, 1.0f, 1.0f, 0.45f};

constexpr Spot::Color WithAlpha(Spot::Color color, float alpha) {
    return {color.r, color.g, color.b, alpha};
}

} // namespace InterfacePalette
