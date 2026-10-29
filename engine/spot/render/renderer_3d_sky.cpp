#include "spot/render/renderer_3d_sky.h"

#include "spot/core/debug/profiler.h"
#include "spot/core/math/angles.h"
#include "spot/render/atmosphere.h"
#include "spot/render/cloud_noise.h"

#include "clouds.frag.h"
#include "clouds_composite.frag.h"
#include "clouds_resolve.frag.h"
#include "clouds_volume.frag.h"
#include "fullscreen.vert.h"
#include "sky.frag.h"
#include "sky.vert.h"
#include "sky_irradiance.frag.h"
#include "sky_prefilter.frag.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <vector>

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
    // The moon's light before the air dims it, with 1 in w when it lights the air.
    Color moonlight;
    Color nightColor;
    // The sun's angular radius, the atmosphere's thickness and the exposure.
    Vector4 settings;
    // 1 when capturing the sky's light for lighting, and the scale of that light.
    Vector4 capture;
    // The stars' brightness, density, rotation and time, the axis they turn around, and the moon's size and
    // brightness.
    Vector4 stars;
    Vector4 starAxis;
    Vector4 moon;
};

struct CloudUniforms {
    // Of the camera's rotation and projection only.
    Matrix4 viewProjection;
    // The camera's position, and the layer's altitude.
    Vector4 view;
    // The clouds' coverage, density, softness and size.
    Vector4 shape;
    // Their offset along X and Z, how many clouds wide the noise is, and how many of its texels a cloud spans.
    Vector4 motion;
    Color color;
    Color skylight;
    // The thickness of their volume, and the frame.
    Vector4 volume;
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
// How much more haze scatters toward the light than its average; see fog.hlsl, which this must match.
constexpr float FogGlowAnisotropy = 0.6f;

Color operator*(Color color, float scale) {
    return {color.r * scale, color.g * scale, color.b * scale, color.a};
}

Color operator*(Color a, Color b) {
    return {a.r * b.r, a.g * b.g, a.b * b.b, a.a};
}

Color operator+(Color a, Color b) {
    return {a.r + b.r, a.g + b.g, a.b + b.b, a.a};
}

float Smoothstep(float from, float to, float x) {
    const float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool IsBlack(Color color) {
    return color.r <= 0.0f && color.g <= 0.0f && color.b <= 0.0f;
}

float GetLuminance(Color color) {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

// An IEEE half float, rounded to the nearest, for the non-negative, finite values of the atmosphere's tables.
std::uint16_t ToHalf(float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(std::max(value, 0.0f));
    const int exponent = static_cast<int>((bits >> 23) & 0xff) - 127 + 15;
    const std::uint32_t mantissa = bits & 0x7fffff;
    if (exponent >= 31) {
        return 0x7bff;
    }
    if (exponent <= 0) {
        if (exponent < -10) {
            return 0;
        }
        // Too small for the exponent: a subnormal, with the leading 1 shifted into the mantissa.
        const std::uint32_t shifted = (mantissa | 0x800000) >> (14 - exponent);
        return static_cast<std::uint16_t>((shifted + 1) >> 1);
    }
    const std::uint32_t half = (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
    return static_cast<std::uint16_t>(half + ((mantissa >> 12) & 1));
}

std::vector<std::uint16_t> ToHalves(const std::vector<Vector3>& texels) {
    std::vector<std::uint16_t> halves;
    halves.reserve(texels.size() * 4);
    for (const Vector3& texel : texels) {
        halves.insert(halves.end(), {ToHalf(texel.x), ToHalf(texel.y), ToHalf(texel.z), ToHalf(1.0f)});
    }
    return halves;
}

Atmosphere GetAtmosphere(const SkyData& sky) {
    return {.thickness = std::max(sky.atmosphereThickness, 0.0f), .skyTint = sky.skyTint};
}

// What of the sky changes how it lights the scene: not its clouds or stars, which move every frame, nor how the moon
// looks, only whether it is shown and how bright it is.
SkyData GetLightingSky(SkyData sky) {
    sky.clouds = {};
    sky.night = {.moonSize = sky.night.moonSize > 0.0f ? 1.0f : 0.0f, .moonBrightness = sky.night.moonBrightness};
    return sky;
}

// The full moon's light above the air: as bright as it looks to eyes used to the dark, as the real moon's, hundreds of
// thousands of times fainter than the sun's, would leave nights black at any exposure that shows the day; and bluish,
// as the dark makes it look. It brightens as the moon rises opposite the sun.
constexpr Color MoonlightColor = {0.062f, 0.075f, 0.1f, 1.0f};

Color GetMoonlight(const SkyData& sky, Vector3 toMoon) {
    if (sky.night.moonSize <= 0.0f || sky.night.moonBrightness <= 0.0f) {
        return Color::Black;
    }
    return MoonlightColor * (sky.night.moonBrightness * Smoothstep(-0.05f, 0.1f, toMoon.y));
}

// Captures are the sky's light for lighting; draws are the sky as seen, into an HDR target or not.
SkyUniforms GetSkyUniforms(const SkyData& sky, const SkyLighting& lighting, bool hdr, bool capture) {
    const Vector3 toSun = -Normalize(sky.sunDirection);
    const bool hasSun = sky.hasSun && toSun != Vector3{};
    const float intensity = hasSun ? sky.sunIntensity : 0.0f;
    return {
        .sunDirection = {toSun.x, toSun.y, toSun.z, hasSun ? 1.0f : 0.0f},
        .sunColor = {sky.sunColor.r * intensity, sky.sunColor.g * intensity, sky.sunColor.b * intensity, 1.0f},
        .skyTint = sky.skyTint,
        .groundColor = sky.groundColor,
        // The ground below the horizon is lit like a surface of the scene facing up.
        .groundLight = lighting.ground,
        .sunLight = lighting.sun ? lighting.sun->color : Color::Black,
        .moonlight = {lighting.moonlight.r, lighting.moonlight.g, lighting.moonlight.b,
                      IsBlack(lighting.moonlight) ? 0.0f : 1.0f},
        .nightColor = sky.nightColor,
        .settings = {std::max(sky.sunSize, 0.0f), std::max(sky.atmosphereThickness, 0.0f), std::max(sky.exposure, 0.0f),
                     hdr ? 1.0f : 0.0f},
        .capture = {capture ? 1.0f : 0.0f, std::max(sky.ambientIntensity, 0.0f), 0.0f, 0.0f},
        .stars = {std::max(sky.night.starBrightness, 0.0f), std::clamp(sky.night.starDensity, 0.0f, 1.0f),
                  sky.night.starRotation, std::fmod(sky.night.time, 3600.0f)},
        .starAxis = ToVector4(LengthSquared(sky.night.starAxis) > 1e-8f ? Normalize(sky.night.starAxis)
                                                                        : Vector3{0.0f, 0.0f, 1.0f}),
        .moon = {std::max(sky.night.moonSize, 0.0f), std::max(sky.night.moonBrightness, 0.0f), 0.0f, 0.0f},
    };
}

CloudUniforms GetCloudUniforms(const SkyData& sky, const SkyLighting& lighting, Vector3 camera,
                               const Matrix4& viewProjection, std::uint32_t frame) {
    const SkyClouds& clouds = sky.clouds;
    return {
        .viewProjection = viewProjection,
        .view = ToVector4(camera, std::max(clouds.height, 0.0f)),
        .shape = {std::clamp(clouds.coverage, 0.0f, 1.0f), std::clamp(clouds.density, 0.0f, 1.0f),
                  std::clamp(clouds.softness, 0.0f, 1.0f), std::max(clouds.size, 1.0f)},
        .motion = {clouds.offset.x, clouds.offset.y, static_cast<float>(CloudNoiseTile),
                   static_cast<float>(CloudNoiseSize / CloudNoiseTile)},
        .color = clouds.color,
        .skylight = lighting.skylight,
        .volume = {std::max(clouds.thickness, 1.0f), static_cast<float>(frame % 1024), 0.0f, 0.0f},
    };
}

} // namespace

SkyLighting GetSkyLighting(const SkyData& sky) {
    const Vector3 toSun = -Normalize(sky.sunDirection);
    SkyLighting lighting{.ambient = sky.nightColor, .skylight = sky.nightColor, .ground = sky.nightColor};
    if (sky.hasSun && toSun != Vector3{}) {
        const Atmosphere atmosphere = GetAtmosphere(sky);
        const Color sunlight = sky.sunColor * sky.sunIntensity;
        const Color direct = sunlight * atmosphere.GetSunTransmittance(toSun, sky.sunSize);
        lighting.sun = LightData{.type = LightType::Directional,
                                 .color = direct,
                                 .direction = sky.sunDirection,
                                 .angularRadius = std::max(sky.sunSize, 0.0f)};
        Color skylight = sunlight * atmosphere.GetSkyIrradiance(toSun);
        Color ground = direct * std::max(toSun.y, 0.0f);

        // The moon lights the night the way the sun lights the day, only far more faintly.
        const Vector3 toMoon = -toSun;
        lighting.moonlight = GetMoonlight(sky, toMoon);
        if (!IsBlack(lighting.moonlight)) {
            const Color moonDirect =
                lighting.moonlight * atmosphere.GetSunTransmittance(toMoon, std::max(sky.night.moonSize, 0.0f));
            if (!IsBlack(moonDirect)) {
                lighting.moon = LightData{.type = LightType::Directional,
                                          .color = moonDirect,
                                          .direction = -sky.sunDirection,
                                          .angularRadius = std::max(sky.night.moonSize, 0.0f)};
            }
            skylight = skylight + lighting.moonlight * atmosphere.GetSkyIrradiance(toMoon);
            ground = ground + moonDirect * std::max(toMoon.y, 0.0f);
        }

        // The sky lights the scene as brightly as it is drawn. Surfaces also face the ground about half as much as the
        // sky, and it reflects the sun.
        const Color groundBounce = sky.groundColor * ground * 0.5f;
        lighting.skylight = lighting.skylight + skylight;
        lighting.ambient = lighting.ambient + (skylight + groundBounce) * sky.ambientIntensity;
        lighting.ground = ground + lighting.ambient;
    }
    lighting.bounce = sky.groundColor * sky.groundColor * lighting.ground * std::max(sky.ambientIntensity, 0.0f);
    return lighting;
}

namespace {

// The sky's light toward a direction above the horizon, as sky.frag.hlsl draws it before the exposure.
Color GetSkyLight(const SkyData& sky, const Atmosphere& atmosphere, Vector3 direction) {
    const Vector3 toSun = -Normalize(sky.sunDirection);
    Color light = sky.nightColor;
    if (sky.hasSun && toSun != Vector3{}) {
        light = light + atmosphere.GetSkyRadiance(direction, toSun) * sky.sunColor * (sky.sunIntensity * Pi);
        const Color moonlight = GetMoonlight(sky, -toSun);
        if (!IsBlack(moonlight)) {
            light = light + atmosphere.GetSkyRadiance(direction, -toSun) * moonlight * Pi;
        }
    }
    return light;
}

float GetFogGlowPhase(float cosAngle) {
    constexpr float G = FogGlowAnisotropy;
    return (1.0f - G * G) / std::pow(std::max(1.0f + G * G - 2.0f * G * cosAngle, 1e-4f), 1.5f);
}

} // namespace

void SetFogFromSky(const SkyData& sky, FogData& fog) {
    const Vector3 toSun = -Normalize(sky.sunDirection);
    if (!sky.hasSun || toSun == Vector3{}) {
        fog.color = sky.nightColor;
        fog.glow = Color::Black;
        return;
    }
    // The haze glows toward the sun by day and toward the moon at night. The sky a few degrees above the horizon,
    // toward the light, away from it and across, gives the fog's color across and the glow that makes the rest.
    const Vector3 toLight = toSun.y > -0.1f ? toSun : -toSun;
    Vector3 level = {toLight.x, 0.0f, toLight.z};
    level = LengthSquared(level) > 1e-6f ? Normalize(level) : Vector3{1.0f, 0.0f, 0.0f};
    const auto atHorizon = [](Vector3 flat) { return Normalize(Vector3{flat.x, 0.05f, flat.z}); };
    const Vector3 toward = atHorizon(level);
    const Vector3 away = atHorizon(-level);
    const Vector3 across = atHorizon({-level.z, 0.0f, level.x});
    const Atmosphere atmosphere = GetAtmosphere(sky);
    const Color towardLight = GetSkyLight(sky, atmosphere, toward);
    const Color awayLight = GetSkyLight(sky, atmosphere, away);
    const Color acrossLight = GetSkyLight(sky, atmosphere, across);
    const float towardPhase = GetFogGlowPhase(Dot(toward, toLight));
    const float awayPhase = GetFogGlowPhase(Dot(away, toLight));
    const float acrossPhase = GetFogGlowPhase(Dot(across, toLight));
    const float spread = std::max(towardPhase - awayPhase, 1e-4f);
    const auto glow = [spread](float a, float b) { return std::max((a - b) / spread, 0.0f); };
    fog.glow = {glow(towardLight.r, awayLight.r), glow(towardLight.g, awayLight.g), glow(towardLight.b, awayLight.b),
                1.0f};
    fog.color = {std::max(acrossLight.r - fog.glow.r * acrossPhase, 0.0f),
                 std::max(acrossLight.g - fog.glow.g * acrossPhase, 0.0f),
                 std::max(acrossLight.b - fog.glow.b * acrossPhase, 0.0f), 1.0f};
    fog.glowDirection = toLight;
}

SkyRenderer::SkyRenderer(Renderer3DState& state) : m_state(state) {
    GraphicsDevice& device = *state.device;
    const auto vertexShader = device.CreateShader(Shaders::SkyVert);
    const auto fragmentShader = device.CreateShader(Shaders::SkyFrag);
    const auto cloudShader = device.CreateShader(Shaders::CloudsFrag);
    if (vertexShader && fragmentShader && cloudShader) {
        m_pipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .targetFormat = state.config.targetFormat,
            // On the far plane, as deep as the cleared depth, so it is drawn only where nothing is in front.
            .depth = DepthState{.format = state.config.depthFormat, .write = false, .compare = CompareOp::LessOrEqual},
            .sampleCount = state.config.sampleCount,
        });
        m_capturePipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .targetFormat = TextureFormat::Rgba16Float,
        });
        m_cloudPipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = cloudShader.get(),
            .blendMode = BlendMode::Premultiplied,
            .targetFormat = state.config.targetFormat,
            // Tested at the depth the shader gives, where each ray meets them, so what is nearer hides them. They write
            // none, so transparent surfaces after them still blend over what is behind them.
            .depth = DepthState{.format = state.config.depthFormat, .write = false, .compare = CompareOp::LessOrEqual},
            .sampleCount = state.config.sampleCount,
        });
    }
    m_cloudSampler = device.CreateSampler({.addressMode = TextureAddressMode::Repeat, .maxAnisotropy = 16.0f});
    m_volumetric = state.config.volumetricClouds && state.config.hdr;
    if (m_volumetric) {
        m_marchPass =
            FullscreenPass::Create(device, Shaders::CloudsVolumeFrag, {.targetFormat = TextureFormat::Rgba16Float});
        m_resolvePass =
            FullscreenPass::Create(device, Shaders::CloudsResolveFrag, {.targetFormat = TextureFormat::Rgba16Float});
        const auto fullscreen = device.CreateShader(Shaders::FullscreenVert);
        const auto composite = device.CreateShader(Shaders::CloudsCompositeFrag);
        if (fullscreen && composite) {
            m_compositePipeline = device.CreatePipeline({
                .vertexShader = fullscreen.get(),
                .fragmentShader = composite.get(),
                .blendMode = BlendMode::Premultiplied,
                .targetFormat = state.config.targetFormat,
                // The march stopped at the surfaces, so the clouds go over the whole view untested.
                .depth = DepthState{.format = state.config.depthFormat, .test = false, .write = false},
                .sampleCount = state.config.sampleCount,
            });
        }
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
    return m_pipeline && m_capturePipeline && m_cloudPipeline && m_prefilter && m_irradiancePass && m_sampler &&
           m_cloudSampler && m_noReflections && m_noIrradiance &&
           (!m_volumetric || (m_marchPass && m_resolvePass && m_compositePipeline));
}

bool SkyRenderer::PrepareTables(const SkyData& sky) {
    std::shared_ptr<const AtmosphereTables> tables = GetAtmosphere(sky).GetTables();
    if (tables == m_tables && m_transmittanceTable && m_scatteringTable) {
        return true;
    }
    GraphicsDevice& device = *m_state.device;
    if (!m_transmittanceTable) {
        m_transmittanceTable = device.CreateTexture({.width = AtmosphereTables::TransmittanceWidth,
                                                     .height = AtmosphereTables::TransmittanceHeight,
                                                     .format = TextureFormat::Rgba16Float});
        m_scatteringTable = device.CreateTexture({.width = AtmosphereTables::ScatteringSize,
                                                  .height = AtmosphereTables::ScatteringSize,
                                                  .format = TextureFormat::Rgba16Float});
    }
    if (!m_transmittanceTable || !m_scatteringTable) {
        return false;
    }
    device.UploadToTexture(*m_transmittanceTable, ToHalves(tables->transmittance));
    device.UploadToTexture(*m_scatteringTable, ToHalves(tables->multipleScattering));
    m_tables = std::move(tables);
    return true;
}

bool SkyRenderer::Update(const SkyData& seen, const SkyLighting& lighting) {
    SPOT_PROFILE_SCOPE("Renderer3D::UpdateSkyLighting");
    const SkyData sky = GetLightingSky(seen);
    if (m_capturedSky == sky) {
        return true;
    }
    if (!PrepareTables(sky)) {
        return false;
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
        pass.BindFragmentTexture(0, *m_transmittanceTable, *m_state.clampSampler);
        pass.BindFragmentTexture(1, *m_scatteringTable, *m_state.clampSampler);
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
    if (!m_transmittanceTable || !m_scatteringTable) {
        return;
    }
    pass.BindPipeline(*m_pipeline);
    pass.BindFragmentTexture(0, *m_transmittanceTable, *m_state.clampSampler);
    pass.BindFragmentTexture(1, *m_scatteringTable, *m_state.clampSampler);
    pass.PushVertexUniforms(0, inverseViewProjection);
    pass.PushFragmentUniforms(0, GetSkyUniforms(sky, lighting, m_state.config.hdr, false));
    pass.PushFragmentUniforms(1, GetFogUniforms(m_state.fog, m_state.camera.position));
    pass.Draw(3);
    ++m_state.stats.drawCalls;
    ++m_state.stats.triangleCount;
}

std::array<Vector4, 2> SkyRenderer::GetCloudShadowUniforms(const SkyData* sky) const {
    if (!sky || !m_cloudNoise || sky->clouds.coverage <= 0.0f || sky->clouds.density <= 0.0f) {
        return {};
    }
    const SkyClouds& clouds = sky->clouds;
    // As a volume, clouds are densest in the lower part of their layer.
    const float altitude =
        std::max(clouds.height, 0.0f) + (m_volumetric ? std::max(clouds.thickness, 1.0f) * 0.35f : 0.0f);
    return {Vector4{std::clamp(clouds.coverage, 0.0f, 1.0f), std::clamp(clouds.density, 0.0f, 1.0f), altitude,
                    std::max(clouds.size, 1.0f)},
            Vector4{clouds.offset.x, clouds.offset.y, static_cast<float>(CloudNoiseTile), 0.0f}};
}

bool SkyRenderer::PrepareClouds(const SkyData& sky) {
    if (sky.clouds.coverage <= 0.0f || sky.clouds.density <= 0.0f) {
        return false;
    }
    if (!m_cloudNoise) {
        GraphicsDevice& device = *m_state.device;
        m_cloudNoise = device.CreateTexture(
            {.width = CloudNoiseSize, .height = CloudNoiseSize, .format = TextureFormat::Rgba8, .mipmaps = true});
        if (!m_cloudNoise) {
            return false;
        }
        device.UploadToTexture(*m_cloudNoise, GetCloudNoise());
    }
    if (m_volumetric && !m_volumeNoise) {
        GraphicsDevice& device = *m_state.device;
        m_volumeNoise = device.CreateTexture({.width = CloudVolumeSize,
                                              .height = CloudVolumeSize,
                                              .format = TextureFormat::Rgba8,
                                              .type = TextureType::Texture3D,
                                              .depth = CloudVolumeSize});
        if (!m_volumeNoise) {
            return false;
        }
        device.UploadToTexture(*m_volumeNoise, GetCloudVolumeNoise());
    }
    return true;
}

void SkyRenderer::DrawClouds(RenderPass& pass, const SkyData& sky, const SkyLighting& lighting,
                             const Matrix4& viewProjection, const Matrix4& inverseViewProjection) {
    pass.BindPipeline(*m_cloudPipeline);
    pass.PushVertexUniforms(0, inverseViewProjection);
    pass.PushFragmentUniforms(0, GetSkyUniforms(sky, lighting, m_state.config.hdr, false));
    pass.PushFragmentUniforms(1, GetFogUniforms(m_state.fog, m_state.camera.position));
    pass.PushFragmentUniforms(2, GetCloudUniforms(sky, lighting, m_state.camera.position, viewProjection, 0));
    pass.BindFragmentTexture(0, *m_cloudNoise, *m_cloudSampler);
    pass.Draw(3);
    ++m_state.stats.drawCalls;
    ++m_state.stats.triangleCount;
}

void SkyRenderer::RenderVolumetricClouds(const SkyData& sky, const SkyLighting& lighting, const Texture& cameraDepth,
                                         const Matrix4& viewProjection, const Matrix4& inverseViewProjection) {
    SPOT_PROFILE_SCOPE("Renderer3D::RenderVolumetricClouds");
    GraphicsDevice& device = *m_state.device;
    RenderContext& context = *m_state.context;
    const std::uint32_t width = std::max((cameraDepth.GetWidth() + 1) / 2, 1u);
    const std::uint32_t height = std::max((cameraDepth.GetHeight() + 1) / 2, 1u);
    if (!m_marched || m_marched->GetWidth() != width || m_marched->GetHeight() != height) {
        const TextureDesc desc{
            .width = width, .height = height, .format = TextureFormat::Rgba16Float, .renderTarget = true};
        m_marched = device.CreateTexture(desc);
        m_resolved = {device.CreateTexture(desc), device.CreateTexture(desc)};
        m_hasHistory = false;
    }
    if (!m_marched || !m_resolved[0] || !m_resolved[1] || !PrepareTables(sky)) {
        return;
    }

    const Vector3 camera = m_state.camera.position;
    const Vector2 offset = sky.clouds.offset;
    const Vector3 moved = camera - m_previousCamera;
    // A camera that jumped far, as at a cut, sees other clouds than before.
    if (Length(moved) > 1000.0f) {
        m_hasHistory = false;
    }
    const Vector2 carried = offset - m_previousOffset;
    const float bottom = std::max(sky.clouds.height, 0.0f);
    const auto size = [](std::uint32_t w, std::uint32_t h) {
        return Vector4{static_cast<float>(w), static_cast<float>(h), 1.0f / static_cast<float>(w),
                       1.0f / static_cast<float>(h)};
    };
    m_frame = {
        .inverseViewProjection = inverseViewProjection,
        .previousViewProjection = m_hasHistory ? m_previousViewProjection : viewProjection,
        .reprojection = {moved.x - carried.x, moved.y, moved.z - carried.y, m_hasHistory ? 1.0f : 0.0f},
        .layer = {camera.y, bottom, bottom + std::max(sky.clouds.thickness, 1.0f), 0.0f},
        .cloudSize = size(width, height),
        .viewSize = size(cameraDepth.GetWidth(), cameraDepth.GetHeight()),
    };
    {
        auto pass = context.BeginRenderPass({.target = m_marched.get()});
        pass.PushFragmentUniforms(0, GetSkyUniforms(sky, lighting, true, false));
        pass.PushFragmentUniforms(1, GetFogUniforms(m_state.fog, camera));
        pass.PushFragmentUniforms(2, GetCloudUniforms(sky, lighting, camera, viewProjection, m_cloudFrame));
        pass.PushFragmentUniforms(3, m_frame);
        const std::array inputs{FullscreenInput{&cameraDepth, m_state.nearestSampler.get()},
                                FullscreenInput{m_volumeNoise.get(), m_cloudSampler.get()},
                                FullscreenInput{m_cloudNoise.get(), m_cloudSampler.get()},
                                FullscreenInput{m_transmittanceTable.get(), m_state.clampSampler.get()},
                                FullscreenInput{m_scatteringTable.get(), m_state.clampSampler.get()}};
        m_marchPass->Draw(pass, inputs);
    }
    const std::uint32_t next = 1 - m_shown;
    {
        auto pass = context.BeginRenderPass({.target = m_resolved[next].get()});
        pass.PushFragmentUniforms(0, m_frame);
        const std::array inputs{FullscreenInput{m_marched.get(), m_state.clampSampler.get()},
                                FullscreenInput{m_resolved[m_shown].get(), m_state.clampSampler.get()}};
        m_resolvePass->Draw(pass, inputs);
    }
    m_shown = next;
    m_hasHistory = true;
    m_previousViewProjection = viewProjection;
    m_previousCamera = camera;
    m_previousOffset = offset;
    ++m_cloudFrame;
}

void SkyRenderer::CompositeClouds(RenderPass& pass, const Texture& cameraDepth) {
    if (!m_resolved[m_shown]) {
        return;
    }
    pass.BindPipeline(*m_compositePipeline);
    pass.PushFragmentUniforms(0, m_frame);
    pass.BindFragmentTexture(0, *m_resolved[m_shown], *m_state.clampSampler);
    pass.BindFragmentTexture(1, cameraDepth, *m_state.nearestSampler);
    pass.Draw(3);
    ++m_state.stats.drawCalls;
    ++m_state.stats.triangleCount;
}

void SkyRenderer::Bind(RenderPass& pass) const {
    pass.BindFragmentTexture(7, m_lit ? *m_reflections : *m_noReflections, *m_sampler);
    pass.BindFragmentTexture(8, m_lit ? *m_irradiance : *m_noIrradiance, *m_sampler);
    pass.BindFragmentTexture(13, m_cloudNoise ? *m_cloudNoise : *m_state.whiteTexture, *m_cloudSampler);
}

} // namespace Spot::Detail

namespace Spot {

namespace {

// How much a camera makes up for dimmer light: fully at 1, while less leaves dusk dimmer than the day and the night
// darker still, as eyes see them; and how many times brighter than under a high sun it exposes at most.
constexpr float ExposureAdaptation = 0.75f;
constexpr float MaxNightExposure = 16.0f;
// The moon's light and the night's are already as bright as eyes used to the dark see them, far brighter than the
// real ones, so they count this many times over: the night is exposed little more than dusk, and looks dark.
constexpr float NightLightWeight = 7.0f;

} // namespace

float GetSkyExposure(const SkyData& sky) {
    const Vector3 toSun = -Normalize(sky.sunDirection);
    const Detail::Atmosphere atmosphere = Detail::GetAtmosphere(sky);
    // What open ground receives, from the sun and the sky it lights, and from the moon and the night sky.
    float day = 0.0f;
    float night = Detail::GetLuminance(sky.nightColor);
    if (sky.hasSun && toSun != Vector3{}) {
        const float sunlight = Detail::GetLuminance(sky.sunColor) * sky.sunIntensity;
        day = sunlight *
              (Detail::GetLuminance(atmosphere.GetSunTransmittance(toSun, sky.sunSize)) * std::max(toSun.y, 0.0f) +
               Detail::GetLuminance(atmosphere.GetSkyIrradiance(toSun)));
        const float moonlight = Detail::GetLuminance(Detail::GetMoonlight(sky, -toSun));
        if (moonlight > 0.0f) {
            night += moonlight * (Detail::GetLuminance(atmosphere.GetSunTransmittance(-toSun, sky.night.moonSize)) *
                                      std::max(-toSun.y, 0.0f) +
                                  Detail::GetLuminance(atmosphere.GetSkyIrradiance(-toSun)));
        }
    }
    const float illuminance = day + night * NightLightWeight;
    // Against the sunlight above the air, so a scene with a brighter sun looks as bright, with what it lights
    // brighter than the rest.
    const float reference = sky.hasSun ? std::max(Detail::GetLuminance(sky.sunColor) * sky.sunIntensity, 1e-6f) : 1.0f;
    const float adapted = reference * std::pow(std::max(illuminance, 1e-6f) / reference, ExposureAdaptation);
    return std::min(1.0f / adapted, MaxNightExposure / reference);
}

} // namespace Spot
