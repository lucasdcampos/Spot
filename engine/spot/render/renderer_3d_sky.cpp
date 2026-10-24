#include "spot/render/renderer_3d_sky.h"

#include "spot/core/debug/profiler.h"
#include "spot/render/atmosphere.h"

#include "sky.frag.h"
#include "sky.vert.h"
#include "sky_irradiance.frag.h"
#include "sky_prefilter.frag.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Spot::Detail {

namespace {

struct SkyUniforms {
    // w is 1 when there is a sun.
    Vector4 sunDirection;
    Color sunColor;
    Color skyTint;
    Color groundColor;
    Color groundLight;
    Color sunLight;
    Color nightColor;
    // The sun's angular radius, the atmosphere's thickness and the exposure.
    Vector4 settings;
    // 1 when capturing the sky's light for lighting, and the scale of that light.
    Vector4 capture;
    // The clouds' coverage, density, height and size; their offset along X and Z and their softness; their color.
    Vector4 clouds;
    Vector4 cloudMotion;
    Color cloudColor;
    // The stars' brightness, density, rotation and time, the axis they turn around, and the moon's size and
    // brightness.
    Vector4 stars;
    Vector4 starAxis;
    Vector4 moon;
};

struct PrefilterUniforms {
    Vector4 forward;
    Vector4 right;
    Vector4 up;
    // The roughness, the size of the environment's faces, and its number of mip levels.
    Vector4 params;
};

// The sky's light is captured at this size, which is plenty for its smooth gradients.
constexpr std::uint32_t CubeSize = 64;
// The sky's irradiance is projected from its faces at this size.
constexpr std::uint32_t IrradianceSamples = 16;

// The directions of each face of a cube texture, as TextureType::Cube lays them out: forward, right and up.
constexpr std::array<std::array<Vector3, 3>, 6> CubeFaces{{
    {{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}}},
    {{{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}},
    {{{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}},
    {{{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}},
    {{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}},
    {{{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}},
}};

Color operator*(Color color, float scale) {
    return {color.r * scale, color.g * scale, color.b * scale, color.a};
}

Color operator*(Color a, Color b) {
    return {a.r * b.r, a.g * b.g, a.b * b.b, a.a};
}

Color operator+(Color a, Color b) {
    return {a.r + b.r, a.g + b.g, a.b + b.b, a.a};
}

// What of the sky changes how it lights the scene: not its clouds, stars or moon, which move every frame.
SkyData GetLightingSky(SkyData sky) {
    sky.clouds = {};
    sky.night = {};
    return sky;
}

// Captures are the sky's light for lighting; draws are the sky as seen, into an HDR target or not.
SkyUniforms GetSkyUniforms(const SkyData& sky, const SkyLighting& lighting, bool hdr, bool capture) {
    const Vector3 toSun = -Normalize(sky.sunDirection);
    const bool hasSun = sky.hasSun && toSun != Vector3{};
    const float intensity = hasSun ? sky.sunIntensity : 0.0f;
    // The ground below the horizon is lit like a surface of the scene facing up.
    Color groundLight = lighting.ambient;
    if (lighting.sun) {
        groundLight = groundLight + lighting.sun->color * std::max(toSun.y, 0.0f);
    }
    return {
        .sunDirection = {toSun.x, toSun.y, toSun.z, hasSun ? 1.0f : 0.0f},
        .sunColor = {sky.sunColor.r * intensity, sky.sunColor.g * intensity, sky.sunColor.b * intensity, 1.0f},
        .skyTint = sky.skyTint,
        .groundColor = sky.groundColor,
        .groundLight = groundLight,
        .sunLight = lighting.sun ? lighting.sun->color : Color::Black,
        .nightColor = sky.nightColor,
        .settings = {std::max(sky.sunSize, 0.0f), std::max(sky.atmosphereThickness, 0.0f), std::max(sky.exposure, 0.0f),
                     hdr ? 1.0f : 0.0f},
        .capture = {capture ? 1.0f : 0.0f, std::max(sky.ambientIntensity, 0.0f), 0.0f, 0.0f},
        .clouds = {std::clamp(sky.clouds.coverage, 0.0f, 1.0f), std::clamp(sky.clouds.density, 0.0f, 1.0f),
                   std::max(sky.clouds.height, 1.0f), std::max(sky.clouds.size, 1.0f)},
        .cloudMotion = {sky.clouds.offset.x, sky.clouds.offset.y, std::clamp(sky.clouds.softness, 0.0f, 1.0f), 0.0f},
        .cloudColor = sky.clouds.color,
        .stars = {std::max(sky.night.starBrightness, 0.0f), std::clamp(sky.night.starDensity, 0.0f, 1.0f),
                  sky.night.starRotation, std::fmod(sky.night.time, 3600.0f)},
        .starAxis = ToVector4(LengthSquared(sky.night.starAxis) > 1e-8f ? Normalize(sky.night.starAxis)
                                                                        : Vector3{0.0f, 0.0f, 1.0f}),
        .moon = {std::max(sky.night.moonSize, 0.0f), std::max(sky.night.moonBrightness, 0.0f), 0.0f, 0.0f},
    };
}

} // namespace

SkyLighting GetSkyLighting(const SkyData& sky) {
    const Vector3 toSun = -Normalize(sky.sunDirection);
    SkyLighting lighting{.sun = std::nullopt, .ambient = sky.nightColor};
    if (!sky.hasSun || toSun == Vector3{}) {
        return lighting;
    }
    const Atmosphere atmosphere{.thickness = std::max(sky.atmosphereThickness, 0.0f), .skyTint = sky.skyTint};
    const Color sunlight = sky.sunColor * sky.sunIntensity;
    const Color direct = sunlight * atmosphere.GetSunTransmittance(toSun, sky.sunSize);
    lighting.sun = LightData{.type = LightType::Directional, .color = direct, .direction = sky.sunDirection};

    // The atmosphere is single scattering, which leaves out the light the air scatters more than once, about as much
    // again near the ground. Surfaces also face the ground about half as much as the sky, and it reflects the sun.
    constexpr float MultipleScattering = 2.0f;
    const Color skylight = sunlight * atmosphere.GetSkyIrradiance(toSun) * MultipleScattering;
    const Color groundBounce = sky.groundColor * direct * (0.5f * std::max(toSun.y, 0.0f));
    lighting.ambient = lighting.ambient + (skylight + groundBounce) * sky.ambientIntensity;
    return lighting;
}

SkyRenderer::SkyRenderer(Renderer3DState& state) : m_state(state) {
    GraphicsDevice& device = *state.device;
    const auto vertexShader = device.CreateShader(Shaders::SkyVert);
    const auto fragmentShader = device.CreateShader(Shaders::SkyFrag);
    if (vertexShader && fragmentShader) {
        m_pipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .targetFormat = state.config.targetFormat,
            .depth = DepthState{.format = state.config.depthFormat, .test = false, .write = false},
            .sampleCount = state.config.sampleCount,
        });
        m_capturePipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .targetFormat = TextureFormat::Rgba16Float,
        });
    }
    m_prefilter =
        FullscreenPass::Create(device, Shaders::SkyPrefilterFrag, {.targetFormat = TextureFormat::Rgba16Float});
    m_irradiancePass =
        FullscreenPass::Create(device, Shaders::SkyIrradianceFrag, {.targetFormat = TextureFormat::Rgba16Float});
    m_sampler = device.CreateSampler();
    m_noReflections = device.CreateTexture(
        {.width = 1, .height = 1, .format = TextureFormat::Rgba16Float, .type = TextureType::Cube});
    if (m_noReflections) {
        device.UploadToTexture(*m_noReflections, std::array<std::uint16_t, 6 * 4>{});
    }
    m_noIrradiance = device.CreateTexture({.width = 9, .height = 1, .format = TextureFormat::Rgba16Float});
    if (m_noIrradiance) {
        device.UploadToTexture(*m_noIrradiance, std::array<std::uint16_t, 9 * 4>{});
    }
}

bool SkyRenderer::IsValid() const {
    return m_pipeline && m_capturePipeline && m_prefilter && m_irradiancePass && m_sampler && m_noReflections &&
           m_noIrradiance;
}

bool SkyRenderer::Update(const SkyData& seen, const SkyLighting& lighting) {
    SPOT_PROFILE_SCOPE("Renderer3D::UpdateSkyLighting");
    const SkyData sky = GetLightingSky(seen);
    if (m_capturedSky == sky) {
        return true;
    }
    GraphicsDevice& device = *m_state.device;
    RenderContext& context = *m_state.context;
    if (!m_environment) {
        m_environment = device.CreateTexture({.width = CubeSize,
                                              .height = CubeSize,
                                              .format = TextureFormat::Rgba16Float,
                                              .type = TextureType::Cube,
                                              .renderTarget = true,
                                              .mipmaps = true});
        m_reflections = device.CreateTexture({.width = CubeSize,
                                              .height = CubeSize,
                                              .format = TextureFormat::Rgba16Float,
                                              .type = TextureType::Cube,
                                              .renderTarget = true,
                                              .mipmaps = true});
        m_irradiance =
            device.CreateTexture({.width = 9, .height = 1, .format = TextureFormat::Rgba16Float, .renderTarget = true});
    }
    if (!m_environment || !m_reflections || !m_irradiance) {
        return false;
    }

    // Each face is drawn with the sky's directions for its texels: clip-space x and y run along its right and up.
    const SkyUniforms uniforms = GetSkyUniforms(sky, lighting, true, true);
    for (std::uint32_t face = 0; face < 6; ++face) {
        const auto& [forward, right, up] = CubeFaces[face];
        Matrix4 directions;
        directions.columns = {ToVector4(right), ToVector4(up), Vector4{}, ToVector4(forward, 1.0f)};
        auto pass = context.BeginRenderPass({.target = m_environment.get(), .targetLayer = face});
        pass.BindPipeline(*m_capturePipeline);
        pass.PushVertexUniforms(0, directions);
        pass.PushFragmentUniforms(0, uniforms);
        pass.PushFragmentUniforms(1, GetFogUniforms(std::nullopt, {}));
        pass.Draw(3);
    }
    context.GenerateMipmaps(*m_environment);

    const FullscreenInput environment{m_environment.get(), m_sampler.get()};
    const float levels = static_cast<float>(m_environment->GetMipLevelCount());
    for (std::uint32_t level = 0; level < ReflectionLevels; ++level) {
        const float roughness = static_cast<float>(level) / static_cast<float>(ReflectionLevels - 1);
        for (std::uint32_t face = 0; face < 6; ++face) {
            const auto& [forward, right, up] = CubeFaces[face];
            auto pass =
                context.BeginRenderPass({.target = m_reflections.get(), .targetLayer = face, .targetMipLevel = level});
            pass.PushFragmentUniforms(
                0, PrefilterUniforms{.forward = ToVector4(forward),
                                     .right = ToVector4(right),
                                     .up = ToVector4(up),
                                     .params = {roughness, static_cast<float>(CubeSize), levels, 0.0f}});
            m_prefilter->Draw(pass, {&environment, 1});
        }
    }

    {
        auto pass = context.BeginRenderPass({.target = m_irradiance.get()});
        const float level = std::log2(static_cast<float>(CubeSize / IrradianceSamples));
        pass.PushFragmentUniforms(0, Vector4{static_cast<float>(CubeSize), level, 0.0f, 0.0f});
        m_irradiancePass->Draw(pass, {&environment, 1});
    }
    m_capturedSky = sky;
    ++m_state.stats.skyLightingUpdates;
    return true;
}

void SkyRenderer::Draw(RenderPass& pass, const SkyData& sky, const SkyLighting& lighting,
                       const Matrix4& inverseViewProjection) {
    pass.BindPipeline(*m_pipeline);
    pass.PushVertexUniforms(0, inverseViewProjection);
    pass.PushFragmentUniforms(0, GetSkyUniforms(sky, lighting, m_state.config.hdr, false));
    pass.PushFragmentUniforms(1, GetFogUniforms(m_state.fog, m_state.camera.position));
    pass.Draw(3);
    ++m_state.stats.drawCalls;
    ++m_state.stats.triangleCount;
}

void SkyRenderer::Bind(RenderPass& pass) const {
    pass.BindFragmentTexture(7, m_lit ? *m_reflections : *m_noReflections, *m_sampler);
    pass.BindFragmentTexture(8, m_lit ? *m_irradiance : *m_noIrradiance, *m_sampler);
}

} // namespace Spot::Detail
