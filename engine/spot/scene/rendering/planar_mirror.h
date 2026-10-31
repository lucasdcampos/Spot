#pragma once

#include "spot/render/camera_3d.h"
#include "spot/scene/component.h"

namespace Spot {

// A rectangular mirror in local XY, facing +Z. Its entity scale sets its size.
class PlanarMirror final : public Component {
public:
    float maxDistance = 12.0f;
    std::uint32_t resolution = 512;
    std::uint32_t reflectionMask = 0xFFFFFFFFu;

    bool IsVisible(const Camera3D& viewer, Vector2 viewSize) const;
    Camera3D GetReflectionCamera(const Camera3D& viewer) const;

protected:
    void Serialize(Archive& archive) override;
};

} // namespace Spot
