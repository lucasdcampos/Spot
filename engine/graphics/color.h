#pragma once

#include <cmath>

namespace Spot {

// Linear RGBA with components in [0, 1]: twice the value is twice the light, which is what lighting and blending
// need. Colors picked from a color picker or an image editor are sRGB-encoded instead; FromSrgb() converts them.
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

    // The linear color of an sRGB-encoded one, such as #808080, which is (0.5, 0.5, 0.5) encoded but about 0.22
    // linear. Alpha is not encoded, so it is kept.
    static Color FromSrgb(float r, float g, float b, float a = 1.0f);

    friend constexpr bool operator==(const Color&, const Color&) = default;
};

inline constexpr Color Color::Black{0.0f, 0.0f, 0.0f, 1.0f};
inline constexpr Color Color::White{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color Color::Red{1.0f, 0.0f, 0.0f, 1.0f};
inline constexpr Color Color::Green{0.0f, 1.0f, 0.0f, 1.0f};
inline constexpr Color Color::Blue{0.0f, 0.0f, 1.0f, 1.0f};
inline constexpr Color Color::Transparent{0.0f, 0.0f, 0.0f, 0.0f};

inline Color Color::FromSrgb(float r, float g, float b, float a) {
    const auto decode = [](float value) {
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    return {decode(r), decode(g), decode(b), a};
}

} // namespace Spot
