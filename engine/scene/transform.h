#pragma once

#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"

namespace Spot {

// Where an entity is, relative to its parent, or to the world for entities without one. Scale applies first, then
// rotation, then position. In 2D, x and y place the entity and the rotation turns around Z.
struct Transform {
    Vector3 position = {};
    Quaternion rotation = {};
    Vector3 scale = {1.0f, 1.0f, 1.0f};

    Matrix4 GetMatrix() const;

    // Sets the rotation to an angle around Z, in radians, counterclockwise.
    void SetRotation2D(float angle) { rotation = Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, angle); }
    // The angle around Z; only meaningful when the rotation is around Z alone.
    float GetRotation2D() const { return ToEuler(rotation).z; }
};

} // namespace Spot
