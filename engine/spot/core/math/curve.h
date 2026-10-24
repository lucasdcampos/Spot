#pragma once

#include <span>
#include <vector>

namespace Spot {
class Archive;

struct CurveKey {
    float time = 0.0f;
    float value = 1.0f;
    float inTangent = 0.0f;
    float outTangent = 0.0f;
    void Serialize(Archive& archive);
    friend bool operator==(const CurveKey&, const CurveKey&) = default;
};

// Cubic Hermite segments. Tangents are derivatives per unit of time; endpoints clamp.
class Curve {
public:
    Curve() : Curve(1.0f) {}
    explicit Curve(float constant);
    explicit Curve(std::vector<CurveKey> keys);
    static Curve Linear(float start, float end);
    float Evaluate(float time) const;
    std::span<const CurveKey> GetKeys() const { return m_keys; }
    void SetKeys(std::vector<CurveKey> keys);
    void Serialize(Archive& archive);

    friend bool operator==(const Curve&, const Curve&) = default;

private:
    void NormalizeKeys();
    std::vector<CurveKey> m_keys;
};
} // namespace Spot
