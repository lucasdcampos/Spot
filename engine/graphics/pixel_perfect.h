#pragma once
#include "math/vector.h"

namespace Spot {
class Archive;
// Integer scaling fills the viewport; surplus pixels reveal more world, and small viewports crop at scale one.
struct PixelPerfectSettings {
    bool enabled = false;
    float pixelsPerUnit = 100;
    Vector2 referenceResolution = {320, 180};
    bool IsValid() const;
    float GetZoom(Vector2 viewSize) const;
    Vector2 SnapCamera(Vector2 position, Vector2 viewSize) const;
    Vector2 SnapQuad(Vector2 position, Vector2 size) const;
    void Serialize(Archive& archive);
};
} // namespace Spot
