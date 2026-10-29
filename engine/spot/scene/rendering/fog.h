#pragma once

#include "spot/core/math/color.h"
#include "spot/render/fog_data.h"
#include "spot/scene/component.h"

namespace Spot {

// Fogs what Renderer3D::DrawScene() draws, the sky included toward the horizon, from the height of its entity: thick
// below it and thinning above it as heightFalloff says. Only the first active fog of a scene is drawn. With a Sky in
// the scene, it takes the sky's color toward the horizon and glows toward the sun, unless colorFromSky is off. See
// FogData for what each setting does.
class Fog final : public Component {
public:
    // Used when colorFromSky is off, or without a sky.
    Color color = FogData{}.color;
    bool colorFromSky = true;
    float density = FogData{}.density;
    float start = FogData{}.start;
    float heightFalloff = FogData{}.heightFalloff;

    // The fog as it is now, at its entity's world height.
    FogData GetFogData() const;

protected:
    void Serialize(Archive& archive) override;
};

} // namespace Spot
