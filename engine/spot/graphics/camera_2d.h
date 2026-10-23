#pragma once

#include "spot/core/matrix.h"
#include "spot/core/vector.h"
#include "spot/graphics/pixel_perfect.h"

namespace Spot {

// A 2D view with Y up. At zoom 1 one world unit covers one unit of the view size, so passing the window size makes
// a world unit one window point, the same unit as mouse positions.
struct Camera2D {
    // The world point at the center of the view.
    Vector2 position = {};
    // Greater than 1 magnifies.
    float zoom = 1.0f;
    // Radians, counterclockwise.
    float rotation = 0.0f;
    PixelPerfectSettings pixelPerfect = {};

    Matrix4 GetViewProjection(Vector2 viewSize) const;

    // Converts a position in the view, such as the mouse position (origin at the top-left, Y down), to world space.
    Vector2 ScreenToWorld(Vector2 screenPosition, Vector2 viewSize) const;
};

} // namespace Spot
