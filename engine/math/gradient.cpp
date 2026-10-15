#include "math/gradient.h"
#include "core/archive.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Spot {
void GradientKey::Serialize(Archive& archive) {
    archive.Field("time", time);
    archive.Field("color", color, {.hdr = true});
}
Gradient::Gradient(Color constant) : m_keys{{0.0f, constant}} {
    NormalizeKeys();
}
Gradient::Gradient(std::vector<GradientKey> keys) {
    SetKeys(std::move(keys));
}
void Gradient::SetKeys(std::vector<GradientKey> keys) {
    m_keys = std::move(keys);
    NormalizeKeys();
}
void Gradient::NormalizeKeys() {
    std::erase_if(m_keys, [](const GradientKey& key) {
        const auto& c = key.color;
        return !std::isfinite(key.time) || !std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b) ||
               !std::isfinite(c.a);
    });
    std::stable_sort(m_keys.begin(), m_keys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    for (std::size_t i = 1; i < m_keys.size();) {
        if (m_keys[i - 1].time == m_keys[i].time) {
            m_keys.erase(m_keys.begin() + static_cast<std::ptrdiff_t>(i - 1));
        } else {
            ++i;
        }
    }
}
Color Gradient::Evaluate(float time) const {
    if (m_keys.empty())
        return Color::White;
    if (std::isnan(time) || time <= m_keys.front().time)
        return m_keys.front().color;
    if (time >= m_keys.back().time)
        return m_keys.back().color;
    const auto next = std::upper_bound(m_keys.begin(), m_keys.end(), time,
                                       [](float at, const GradientKey& key) { return at < key.time; });
    const auto& a = *(next - 1);
    const auto& b = *next;
    const float t = (time - a.time) / (b.time - a.time);
    return {Lerp(a.color.r, b.color.r, t), Lerp(a.color.g, b.color.g, t), Lerp(a.color.b, b.color.b, t),
            Lerp(a.color.a, b.color.a, t)};
}
void Gradient::Serialize(Archive& archive) {
    archive.Field("keys", m_keys);
    if (archive.IsLoading())
        NormalizeKeys();
}
} // namespace Spot
