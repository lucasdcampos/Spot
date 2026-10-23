#pragma once

#include <numbers>

namespace Spot {

inline constexpr float Pi = std::numbers::pi_v<float>;

constexpr float ToRadians(float degrees) {
    return degrees * (Pi / 180.0f);
}

constexpr float ToDegrees(float radians) {
    return radians * (180.0f / Pi);
}

} // namespace Spot
