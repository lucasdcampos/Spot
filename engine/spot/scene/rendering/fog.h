#pragma once

#include "spot/core/color.h"
#include "spot/render/fog_data.h"
#include "spot/scene/component.h"

namespace Spot {

// Fogs what Renderer3D::DrawScene() draws, the sky included toward the horizon, from the height of its entity: thick
// below it and thinning above it as heightFalloff says. Only the first active fog of a scene is drawn. See FogData
// for what each setting does.
class Fog final : public Component {
public:
    Color color = FogData{}.color;
    float density = FogData{}.density;
    float start = FogData{}.start;
    float heightFalloff = FogData{}.heightFalloff;

    // The fog as it is now, at its entity's world height.
    FogData GetFogData() const;

protected:
    void Serialize(Archive& archive) override;
};

} // namespace Spot
