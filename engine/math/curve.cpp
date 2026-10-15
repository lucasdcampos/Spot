#include "math/curve.h"
#include "core/archive.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Spot {
void CurveKey::Serialize(Archive& archive) {
    archive.Field("time", time);
    archive.Field("value", value);
    archive.Field("inTangent", inTangent);
    archive.Field("outTangent", outTangent);
}
Curve::Curve(float constant) : m_keys{{0.0f, std::isfinite(constant) ? constant : 1.0f}} {
}
Curve::Curve(std::vector<CurveKey> keys) {
    SetKeys(std::move(keys));
}
Curve Curve::Linear(float start, float end) {
    return Curve({{0.0f, start, end - start, end - start}, {1.0f, end, end - start, end - start}});
}
void Curve::SetKeys(std::vector<CurveKey> keys) {
    m_keys = std::move(keys);
    NormalizeKeys();
}
void Curve::NormalizeKeys() {
    std::erase_if(m_keys, [](const CurveKey& key) {
        return !std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.inTangent) ||
               !std::isfinite(key.outTangent);
    });
    std::stable_sort(m_keys.begin(), m_keys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    // Keep the last authored key at a duplicate time.
    for (std::size_t i = 1; i < m_keys.size();) {
        if (m_keys[i - 1].time == m_keys[i].time) {
            m_keys.erase(m_keys.begin() + static_cast<std::ptrdiff_t>(i - 1));
        } else {
            ++i;
        }
    }
}
float Curve::Evaluate(float time) const {
    if (m_keys.empty())
        return 1.0f;
    if (std::isnan(time) || time <= m_keys.front().time)
        return m_keys.front().value;
    if (time >= m_keys.back().time)
        return m_keys.back().value;
    const auto next = std::upper_bound(m_keys.begin(), m_keys.end(), time,
                                       [](float at, const CurveKey& key) { return at < key.time; });
    const auto& a = *(next - 1);
    const auto& b = *next;
    const float duration = b.time - a.time;
    const float t = (time - a.time) / duration;
    const float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a.value + (t3 - 2 * t2 + t) * duration * a.outTangent +
           (-2 * t3 + 3 * t2) * b.value + (t3 - t2) * duration * b.inTangent;
}
void Curve::Serialize(Archive& archive) {
    archive.Field("keys", m_keys);
    if (archive.IsLoading())
        NormalizeKeys();
}
} // namespace Spot
