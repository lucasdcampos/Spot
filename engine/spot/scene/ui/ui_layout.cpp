#include "spot/scene/ui/ui_layout.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/io/archive.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Spot {

namespace {
float Finite(float value, float fallback = 0) {
    return std::isfinite(value) ? value : fallback;
}
Vector2 Normalized(Vector2 value) {
    return {std::clamp(Finite(value.x), 0.0f, 1.0f), std::clamp(Finite(value.y), 0.0f, 1.0f)};
}
} // namespace

bool UiBounds::Contains(Vector2 point) const {
    return !IsEmpty() && point.x >= min.x && point.y >= min.y && point.x <= max.x && point.y <= max.y;
}
bool UiBounds::IsEmpty() const {
    return max.x <= min.x || max.y <= min.y;
}
UiBounds IntersectUiBounds(UiBounds a, UiBounds b) {
    const Vector2 min{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y)};
    return {min, {std::max(min.x, std::min(a.max.x, b.max.x)), std::max(min.y, std::min(a.max.y, b.max.y))}};
}

float ComputeUiScale(const UiCanvasSettings& settings, Vector2 viewSize) {
    float scale = std::clamp(Finite(settings.scale, 1), 0.01f, 100.0f);
    if (settings.scaleMode == UiScaleMode::ReferenceResolution) {
        const float width = std::max(1.0f, Finite(settings.referenceResolution.x, 1280));
        const float height = std::max(1.0f, Finite(settings.referenceResolution.y, 720));
        scale *= std::max(0.01f, std::min(Finite(viewSize.x) / width, Finite(viewSize.y) / height));
    }
    return scale;
}

UiBounds ComputeUiRect(const UiRectLayout& layout, UiBounds parent) {
    const Vector2 a = Normalized(layout.anchorMin);
    const Vector2 b = Normalized(layout.anchorMax);
    const Vector2 lower{std::min(a.x, b.x), std::min(a.y, b.y)};
    const Vector2 upper{std::max(a.x, b.x), std::max(a.y, b.y)};
    const Vector2 pivot = Normalized(layout.pivot);
    const Vector2 parentSize = parent.GetSize();
    const Vector2 stretch{parentSize.x * (upper.x - lower.x), parentSize.y * (upper.y - lower.y)};
    const Vector2 size{std::max(0.0f, stretch.x + Finite(layout.sizeDelta.x)),
                       std::max(0.0f, stretch.y + Finite(layout.sizeDelta.y))};
    const Vector2 point{parent.min.x + parentSize.x * lower.x + stretch.x * pivot.x + Finite(layout.offset.x),
                        parent.min.y + parentSize.y * lower.y + stretch.y * pivot.y + Finite(layout.offset.y)};
    const Vector2 min{point.x - size.x * pivot.x, point.y - size.y * pivot.y};
    return {min, min + size};
}

void UiCanvasSettings::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 2> Modes{"Constant Pixels", "Reference Resolution"};
    archive.Field("scaleMode", scaleMode, {.enumNames = Modes});
    archive.Field("referenceResolution", referenceResolution);
    archive.Field("scale", scale, {.min = 0.01f, .max = 100.0f});
    archive.Field("order", order);
    archive.Field("modal", modal);
    static const auto Keys = [] {
        std::array<std::string_view, static_cast<std::size_t>(Key::RightSuper) + 1> names{};
        for (std::size_t index = 0; index < names.size(); ++index)
            names[index] = GetKeyName(static_cast<Key>(index));
        return names;
    }();
    archive.Field("dismissKey", dismissKey, {.enumNames = Keys});
}
void UiRectLayout::Serialize(Archive& archive) {
    archive.Field("anchorMin", anchorMin, {.min = 0.0f, .max = 1.0f});
    archive.Field("anchorMax", anchorMax, {.min = 0.0f, .max = 1.0f});
    archive.Field("pivot", pivot, {.min = 0.0f, .max = 1.0f});
    archive.Field("offset", offset);
    archive.Field("sizeDelta", sizeDelta);
}

} // namespace Spot
