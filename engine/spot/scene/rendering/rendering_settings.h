#pragma once

#include "spot/render/ambient_occlusion.h"
#include "spot/render/bloom.h"
#include "spot/render/tonemapper.h"

#include <cstdint>
#include <string_view>

namespace Spot {

class Archive;
struct SceneRendererConfig;

// The rendering section of a game's settings: the look scenes have without a PostProcessing component, and the
// quality the application draws them at.
struct RenderingSettings {
    static constexpr std::string_view SectionName = "rendering";

    TonemapSettings tonemapping = {};
    BloomSettings bloom = {};
    // Grading without a LUT, which only PostProcessing components hold.
    GradingSettings grading = {};
    bool fxaa = false;
    // Samples per pixel for multisampled antialiasing: 1, 2, 4 or 8, as the GPU allows.
    std::uint32_t msaaSamples = 4;
    // See Renderer3DConfig.
    std::uint32_t shadowMapSize = 2048;
    std::uint32_t shadowCascadeCount = 4;
    float shadowDistance = 60.0f;
    std::uint32_t localShadowMapSize = 512;
    bool gpuCulling = false;
    // See Renderer3DConfig.
    AmbientOcclusionSettings ambientOcclusion = {.enabled = true};
    std::uint32_t skyOcclusionSize = 512;
    float skyOcclusionDistance = 64.0f;

    void Serialize(Archive& archive);
    // Sets the config's look and quality from the settings.
    void Apply(SceneRendererConfig& config) const;
};

} // namespace Spot
