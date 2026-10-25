#include "spot/scene/rendering/rendering_settings.h"

#include "spot/core/io/archive.h"
#include "spot/scene/rendering/scene_renderer.h"

#include <algorithm>
#include <array>

namespace Spot {

void RenderingSettings::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 3> Operators = {"ACES", "AgX", "Linear"};
    archive.Field("tonemapper", tonemapping.op,
                  {.enumNames = Operators, .tooltip = "Without a PostProcessing component"});
    archive.Field("exposure", tonemapping.exposure, {.min = 0.0f, .max = 16.0f, .speed = 0.01f});
    archive.Field("dither", tonemapping.dither, {.tooltip = "Hides the banding of smooth gradients"});
    archive.Field("bloomIntensity", bloom.intensity, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field("bloomThreshold", bloom.threshold, {.min = 0.0f, .max = 16.0f, .speed = 0.01f});
    archive.Field("bloomScatter", bloom.scatter, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("contrast", grading.contrast, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field("saturation", grading.saturation, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field("temperature", grading.temperature, {.min = -1.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("tint", grading.tint, {.min = -1.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("vignette", grading.vignette, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("vignetteStart", grading.vignetteStart, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("fxaa", fxaa,
                  {.tooltip = "Smooths the edges multisampling does not reach, softening the image a little"});
    archive.Field("msaaSamples", msaaSamples,
                  {.min = 1.0f, .max = 8.0f, .tooltip = "Samples per pixel: 1, 2, 4 or 8, as the GPU allows"});
    archive.Field(
        "shadowMapSize", shadowMapSize,
        {.min = 0.0f, .max = 8192.0f, .tooltip = "Pixels of each cascade of the sun's shadows; 0 turns them off"});
    archive.Field("shadowCascadeCount", shadowCascadeCount, {.min = 1.0f, .max = 4.0f});
    archive.Field("shadowDistance", shadowDistance,
                  {.min = 1.0f, .max = 10000.0f, .speed = 0.5f, .tooltip = "How far from the camera shadows reach"});
    archive.Field(
        "localShadowMapSize", localShadowMapSize,
        {.min = 0.0f, .max = 4096.0f, .tooltip = "Pixels of each point and spot light shadow; 0 turns them off"});
    archive.Field("gpuCulling", gpuCulling,
                  {.tooltip = "Culls the scene's meshes on the GPU, for scenes of many objects"});
    archive.Field("ambientOcclusion", ambientOcclusion.enabled,
                  {.tooltip = "Darkens the light from all around in creases and corners"});
    archive.Field(
        "ambientOcclusionRadius", ambientOcclusion.radius,
        {.min = 0.01f, .max = 10.0f, .speed = 0.01f, .tooltip = "How far apart surfaces still darken each other"});
    archive.Field("ambientOcclusionIntensity", ambientOcclusion.intensity, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field(
        "skyOcclusionSize", skyOcclusionSize,
        {.min = 0.0f,
         .max = 4096.0f,
         .tooltip = "Pixels of the map that keeps the sky's light out of what is under cover; 0 turns it off"});
    archive.Field("skyOcclusionDistance", skyOcclusionDistance,
                  {.min = 1.0f, .max = 10000.0f, .speed = 0.5f, .tooltip = "How far from the camera it reaches"});
    if (archive.IsLoading()) {
        grading.lut = nullptr;
    }
}

void RenderingSettings::Apply(SceneRendererConfig& config) const {
    config.tonemapping = tonemapping;
    config.bloom = bloom;
    config.grading = grading;
    config.grading.lut = nullptr;
    config.fxaa = fxaa;
    config.sampleCount = std::clamp(msaaSamples, 1u, 8u);
    config.shadowMapSize = shadowMapSize;
    config.shadowCascadeCount = std::clamp(shadowCascadeCount, 1u, 4u);
    config.shadowDistance = std::max(shadowDistance, 1.0f);
    config.localShadowMapSize = localShadowMapSize;
    config.gpuCulling = gpuCulling;
    config.ambientOcclusion = ambientOcclusion;
    config.skyOcclusionSize = skyOcclusionSize;
    config.skyOcclusionDistance = std::max(skyOcclusionDistance, 1.0f);
}

} // namespace Spot
