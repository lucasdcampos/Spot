#pragma once

#include "spot/core/asset_ref.h"
#include "spot/graphics/texture.h"
#include "spot/render/bloom.h"
#include "spot/render/tonemapper.h"
#include "spot/scene/component.h"

#include <span>

namespace Spot {

class Scene;

// The look of the scenes drawn with it, in place of the rendering settings' defaults: the tonemapper's operator and
// exposure, bloom and grading. The first one active among the scenes drawn is used, as with Sky. See TonemapSettings,
// BloomSettings and GradingSettings for what each setting does.
class PostProcessing final : public Component {
public:
    TonemapOperator tonemapper = TonemapSettings{}.op;
    float exposure = TonemapSettings{}.exposure;

    float bloomIntensity = BloomSettings{}.intensity;
    float bloomThreshold = BloomSettings{}.threshold;
    float bloomScatter = BloomSettings{}.scatter;

    float contrast = GradingSettings{}.contrast;
    float saturation = GradingSettings{}.saturation;
    float temperature = GradingSettings{}.temperature;
    float tint = GradingSettings{}.tint;
    float vignette = GradingSettings{}.vignette;
    float vignetteStart = GradingSettings{}.vignetteStart;
    // A color cube in the strip format GradingSettings::lut describes, imported as data rather than color.
    AssetRef<Texture> lut;
    float lutContribution = GradingSettings{}.lutContribution;

    TonemapSettings GetTonemapping() const;
    BloomSettings GetBloom() const;
    // Valid while the component and its LUT are.
    GradingSettings GetGrading() const;

    // The first active one among the scenes; null when none has one.
    static const PostProcessing* Find(std::span<Scene* const> scenes);

protected:
    void Serialize(Archive& archive) override;
};

} // namespace Spot
