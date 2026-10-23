#include "spot/render/pixel_perfect.h"
#include "spot/core/archive.h"
#include <algorithm>
#include <cmath>

namespace Spot {
bool PixelPerfectSettings::IsValid() const {
    return std::isfinite(pixelsPerUnit) && pixelsPerUnit > 0 && pixelsPerUnit <= 1000000 &&
           std::isfinite(referenceResolution.x) && std::isfinite(referenceResolution.y) && referenceResolution.x >= 1 &&
           referenceResolution.y >= 1 && referenceResolution.x <= 16384 && referenceResolution.y <= 16384;
}
float PixelPerfectSettings::GetZoom(Vector2 viewSize) const {
    if (!IsValid() || !std::isfinite(viewSize.x) || !std::isfinite(viewSize.y) || viewSize.x <= 0 || viewSize.y <= 0)
        return 1;
    const float scale =
        std::max(1.0f, std::floor(std::min(viewSize.x / referenceResolution.x, viewSize.y / referenceResolution.y)));
    return pixelsPerUnit * scale;
}
Vector2 PixelPerfectSettings::SnapCamera(Vector2 position, Vector2 viewSize) const {
    if (!IsValid() || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(viewSize.x) ||
        !std::isfinite(viewSize.y) || viewSize.x <= 0 || viewSize.y <= 0)
        return position;
    const double scale = static_cast<double>(GetZoom(viewSize)) / pixelsPerUnit;
    const auto snap = [&](float value, float pixels) {
        const double half = static_cast<double>(pixels) / (2 * scale);
        return static_cast<float>((std::round(static_cast<double>(value) * pixelsPerUnit - half) + half) /
                                  pixelsPerUnit);
    };
    return {snap(position.x, viewSize.x), snap(position.y, viewSize.y)};
}
Vector2 PixelPerfectSettings::SnapQuad(Vector2 position, Vector2 size) const {
    if (!IsValid())
        return position;
    const auto snap = [&](float value, float extent) {
        const double half = std::abs(static_cast<double>(extent)) * 0.5;
        return static_cast<float>(std::round((static_cast<double>(value) - half) * pixelsPerUnit) / pixelsPerUnit +
                                  half);
    };
    return {snap(position.x, size.x), snap(position.y, size.y)};
}
void PixelPerfectSettings::Serialize(Archive& archive) {
    archive.Field("enabled", enabled);
    archive.Field("pixelsPerUnit", pixelsPerUnit, {.min = 0.001f, .max = 1000000});
    archive.Field("referenceResolution", referenceResolution, {.min = 1, .max = 16384});
}
} // namespace Spot
