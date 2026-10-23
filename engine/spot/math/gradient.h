#pragma once

#include "spot/math/color.h"
#include <span>
#include <vector>

namespace Spot {
class Archive;
struct GradientKey {
    float time = 0.0f;
    Color color = Color::White;
    void Serialize(Archive& archive);
    friend bool operator==(const GradientKey&, const GradientKey&) = default;
};

// Interpolates linear HDR colors and alpha together, clamping outside the authored keys.
class Gradient {
public:
    Gradient() : Gradient(Color::White) {}
    explicit Gradient(Color constant);
    explicit Gradient(std::vector<GradientKey> keys);
    Color Evaluate(float time) const;
    std::span<const GradientKey> GetKeys() const { return m_keys; }
    void SetKeys(std::vector<GradientKey> keys);
    void Serialize(Archive& archive);

    friend bool operator==(const Gradient&, const Gradient&) = default;

private:
    void NormalizeKeys();
    std::vector<GradientKey> m_keys;
};
} // namespace Spot
