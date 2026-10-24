#include "spot/scene/rendering/fog.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/io/archive.h"
#include "spot/scene/entity.h"

namespace Spot {

FogData Fog::GetFogData() const {
    return {.color = color,
            .density = density,
            .start = start,
            .heightFalloff = heightFalloff,
            .baseHeight = GetEntity().GetTransform().GetWorldPosition().y};
}

void Fog::Serialize(Archive& archive) {
    archive.Field("color", color, {.hdr = true, .tooltip = "What far away things fade into; above 1 for lit fog"});
    archive.Field("density", density,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.0005f, .tooltip = "How thick it is at its entity's height"});
    archive.Field("start", start,
                  {.min = 0.0f, .max = 10000.0f, .speed = 0.1f, .tooltip = "Clear up to this distance"});
    archive.Field(
        "heightFalloff", heightFalloff,
        {.min = 0.0f, .max = 1.0f, .speed = 0.001f, .tooltip = "How fast it thins above its entity; 0 hides the sky"});
}

} // namespace Spot
