#pragma once

#include "graphics/color.h"
#include <span>
#include <vector>

namespace Spot {
class Archive;
struct GradientKey {
    float time = 0.0f;
    Color color = Color::White;
    void Serialize(Archive& archive);
};

// Interpolates linear HDR colors and alpha together, clamping outside the authored keys.
class Gradient {
public:
    explicit Gradient(Color constant = Color::White);
    explicit Gradient(std::vector<GradientKey> keys);
    Color Evaluate(float time) const;
    std::span<const GradientKey> GetKeys() const { return m_keys; }
    void SetKeys(std::vector<GradientKey> keys);
    void Serialize(Archive& archive);

private:
    void NormalizeKeys();
    std::vector<GradientKey> m_keys;
};
} // namespace Spot
