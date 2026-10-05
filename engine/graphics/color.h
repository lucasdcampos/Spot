#pragma once

namespace Spot {

// Linear RGBA with components in [0, 1].
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    static const Color Black;
    static const Color White;
    static const Color Red;
    static const Color Green;
    static const Color Blue;
    static const Color Transparent;

    friend constexpr bool operator==(const Color&, const Color&) = default;
};

inline constexpr Color Color::Black{0.0f, 0.0f, 0.0f, 1.0f};
inline constexpr Color Color::White{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color Color::Red{1.0f, 0.0f, 0.0f, 1.0f};
inline constexpr Color Color::Green{0.0f, 1.0f, 0.0f, 1.0f};
inline constexpr Color Color::Blue{0.0f, 0.0f, 1.0f, 1.0f};
inline constexpr Color Color::Transparent{0.0f, 0.0f, 0.0f, 0.0f};

} // namespace Spot
