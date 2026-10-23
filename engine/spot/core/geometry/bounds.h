#pragma once

#include "spot/core/vector.h"

namespace Spot {

// A sphere holding something, such as every vertex of a mesh, so renderers can skip it when it is out of view.
struct BoundingSphere {
    Vector3 center = {};
    float radius = 0.0f;
};

// A box aligned with the axes holding something, such as every vertex of a mesh.
struct BoundingBox {
    Vector3 min = {};
    Vector3 max = {};

    Vector3 GetCenter() const { return (min + max) * 0.5f; }
    Vector3 GetSize() const { return max - min; }
};

} // namespace Spot
