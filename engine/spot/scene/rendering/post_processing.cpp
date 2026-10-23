#include "spot/scene/rendering/post_processing.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/archive.h"
#include "spot/scene/scene.h"

#include <array>
#include <string_view>

namespace Spot {

TonemapSettings PostProcessing::GetTonemapping() const {
    return {.op = tonemapper, .exposure = exposure};
}

BloomSettings PostProcessing::GetBloom() const {
    return {.intensity = bloomIntensity, .threshold = bloomThreshold, .scatter = bloomScatter};
}

GradingSettings PostProcessing::GetGrading() const {
    return {.contrast = contrast,
            .saturation = saturation,
            .temperature = temperature,
            .tint = tint,
            .vignette = vignette,
            .vignetteStart = vignetteStart,
            .lut = lut.Get(),
            .lutContribution = lutContribution};
}

const PostProcessing* PostProcessing::Find(std::span<Scene* const> scenes) {
    const PostProcessing* found = nullptr;
    for (Scene* scene : scenes) {
        scene->ForEachComponent<PostProcessing>(
            [&found](const PostProcessing& look) { found = found ? found : &look; });
    }
    return found;
}

void PostProcessing::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 3> Operators = {"ACES", "AgX", "Linear"};
    archive.Field("tonemapper", tonemapper,
                  {.enumNames = Operators, .tooltip = "How light beyond what the display shows is brought within it"});
    archive.Field("exposure", exposure,
                  {.min = 0.0f, .max = 16.0f, .speed = 0.01f, .tooltip = "Multiplies the light; 2 is a stop brighter"});
    archive.Field("bloomIntensity", bloomIntensity,
                  {.min = 0.0f, .max = 4.0f, .speed = 0.01f, .tooltip = "How much glow is added; 0 turns bloom off"});
    archive.Field("bloomThreshold", bloomThreshold,
                  {.min = 0.0f, .max = 16.0f, .speed = 0.01f, .tooltip = "Light below this does not glow"});
    archive.Field(
        "bloomScatter", bloomScatter,
        {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "From a tight glow to one over the whole image"});
    archive.Field("contrast", contrast, {.min = 0.0f, .max = 4.0f, .speed = 0.01f, .tooltip = "Around middle grey"});
    archive.Field("saturation", saturation, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field("temperature", temperature,
                  {.min = -1.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "From cooler to warmer light"});
    archive.Field("tint", tint, {.min = -1.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "From greener to more magenta"});
    archive.Field("vignette", vignette,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "How dark the corners get"});
    archive.Field("vignetteStart", vignetteStart,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "From the middle (0) to the corners (1)"});
    archive.Field("lut", lut, {.tooltip = "A color cube as a strip of slices, imported as data rather than color"});
    archive.Field("lutContribution", lutContribution, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
}

} // namespace Spot
