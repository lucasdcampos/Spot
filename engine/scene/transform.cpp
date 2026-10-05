#include "scene/transform.h"

namespace Spot {

Matrix4 Transform::GetMatrix() const {
    return Matrix4::Translation(position) * Matrix4::Rotation(rotation) * Matrix4::Scale(scale);
}

} // namespace Spot
