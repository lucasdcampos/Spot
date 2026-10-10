#include "graphics/renderer_3d.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/profiler.h"
#include "graphics/atmosphere.h"
#include "graphics/graphics_device.h"
#include "graphics/material.h"
#include "graphics/mesh.h"
#include "graphics/model.h"
#include "graphics/render_context.h"
#include "graphics/shadow_views.h"
#include "scene/fog.h"
#include "scene/light.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"
#include "scene/sky.h"

#include "mesh.frag.h"
#include "mesh.vert.h"
#include "shadow.frag.h"
#include "shadow.vert.h"
#include "sky.frag.h"
#include "sky.vert.h"
#include "sky_irradiance.frag.h"
#include "sky_prefilter.frag.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

namespace Spot {

namespace {

// These mirror the shaders' constant buffers.
struct ObjectUniforms {
    Matrix4 model;
    Matrix4 normalMatrix;
};

struct LightUniforms {
    // w is the type: 0 for directional, 1 for point and 2 for spot.
    Vector4 positionAndType;
    Vector4 directionAndRange;
    // Premultiplied by the intensity.
    Color color;
    // The cosine of the outer cone angle, the inverse of how much greater the inner one's is, the first shadow tile
    // or -1, and the size of the shadow's texels per unit of distance.
    Vector4 cone;
};

struct LightingUniforms {
    Color ambient;
    Vector4 cameraPosition;
    // x is the number of lights; a whole row keeps the layout the same in every shader language.
    std::array<std::uint32_t, 4> lightCount;
    // 1 when the sky lights the scene, and the mip level of its reflections for a roughness of 1.
    Vector4 skyLighting;
    std::array<LightUniforms, Renderer3D::MaxLights> lights;
};

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
};

struct PrefilterUniforms {
    Vector4 forward;
    Vector4 right;
    Vector4 up;
    // The roughness, the size of the environment's faces, and its number of mip levels.
    Vector4 params;
};

// The sky's light is captured at this size, which is plenty for its smooth gradients, and blurred into this many
// levels of roughness, from 0 to 1.
constexpr std::uint32_t SkyCubeSize = 64;
constexpr std::uint32_t SkyReflectionLevels = 5;
// The sky's irradiance is projected from its faces at this size.
constexpr std::uint32_t SkyIrradianceSamples = 16;

// The directions of each face of a cube texture, as TextureType::Cube lays them out: forward, right and up.
constexpr std::array<std::array<Vector3, 3>, 6> CubeFaces{{
    {{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}}},
    {{{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}},
    {{{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}},
    {{{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}},
    {{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}},
    {{{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}},
}};

Vector4 ToVector4(Vector3 v, float w = 0.0f) {
    return {v.x, v.y, v.z, w};
}

Color operator*(Color color, float scale) {
    return {color.r * scale, color.g * scale, color.b * scale, color.a};
}

Color operator*(Color a, Color b) {
    return {a.r * b.r, a.g * b.g, a.b * b.b, a.a};
}

Color operator+(Color a, Color b) {
    return {a.r + b.r, a.g + b.g, a.b + b.b, a.a};
}

} // namespace

namespace Detail {

// How a sky lights what is under it: the sun through the air, which dims and reddens it near the horizon and hides it
// below, and the light the rest of the sky scatters down.
struct SkyLighting {
    std::optional<LightData> sun;
    Color ambient;
};

} // namespace Detail

namespace {

using Detail::SkyLighting;

SkyLighting GetSkyLighting(const SkyData& sky) {
    const Vector3 toSun = -Normalize(sky.sunDirection);
    SkyLighting lighting{.sun = std::nullopt, .ambient = sky.nightColor};
    if (!sky.hasSun || toSun == Vector3{}) {
        return lighting;
    }
    const Detail::Atmosphere atmosphere{.thickness = std::max(sky.atmosphereThickness, 0.0f), .skyTint = sky.skyTint};
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

struct FogUniforms {
    Color color;
    Vector4 params;
    Vector4 camera;
};

FogUniforms GetFogUniforms(const std::optional<FogData>& fog, Vector3 cameraPosition) {
    if (!fog) {
        return {.color = {0.0f, 0.0f, 0.0f, 0.0f}, .params = {}, .camera = {}};
    }
    return {.color = {fog->color.r, fog->color.g, fog->color.b, 1.0f},
            .params = {std::max(fog->density, 0.0f), std::max(fog->start, 0.0f), std::max(fog->heightFalloff, 0.0f),
                       fog->baseHeight},
            .camera = {cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f}};
}

struct MaterialUniforms {
    Color baseColor;
    Color emissive;
    float metallic;
    float roughness;
    // 0 without a normal map, so the white texture bound in its place leaves the normals as they are.
    float normalScale;
    float occlusionStrength;
    float doubleSided;
    float worldTextureSize;
    float padding[2];
};

// How much a light matters to what is near the camera, lowest first: directional lights reach everything.
float GetLightRank(const LightData& light, Vector3 cameraPosition) {
    return light.type == LightType::Directional
               ? -1.0f
               : std::max(Distance(light.position, cameraPosition) - light.range, 0.0f);
}

// Takes world positions to the UVs of a shadow tile, Y down, keeping the depth.
const Matrix4 TileFromClip = Matrix4::Translation({0.5f, 0.5f, 0.0f}) * Matrix4::Scale({0.5f, -0.5f, 1.0f});

} // namespace

namespace Detail {

struct SelectedLights {
    Color ambient = Color::Black;
    // At most MaxLights, none of them ambient.
    std::vector<const LightData*> lights;
    // Per light: 0 or more when it casts shadows, as LightUniforms::cone holds it.
    std::array<float, Renderer3D::MaxLights> shadows{};
    std::array<float, Renderer3D::MaxLights> shadowTexelSizes{};
};

struct ShadowUniforms {
    std::array<Matrix4, Detail::MaxShadowCascades> cascadeViews;
    std::array<Matrix4, Renderer3D::MaxShadowTiles> tileViews;
    Vector4 cascadeEnds;
    Vector4 cascadeTexelSizes;
    // w is the number of cascades.
    Vector4 cameraForward;
    // The columns and rows of tiles in each shadow map, and the size of their texels in UVs.
    Vector4 cascadeLayout;
    Vector4 tileLayout;
};

} // namespace Detail

namespace {

// Ambient lights are summed. Past MaxLights, the other lights that matter least to what is near the camera are dropped.
Detail::SelectedLights SelectLights(const std::vector<LightData>& allLights, Vector3 cameraPosition,
                                    bool& warnedAboutLights) {
    Detail::SelectedLights selected;
    selected.ambient.a = 1.0f;
    selected.shadows.fill(-1.0f);
    for (const auto& light : allLights) {
        if (light.type == LightType::Ambient) {
            selected.ambient.r += light.color.r * light.intensity;
            selected.ambient.g += light.color.g * light.intensity;
            selected.ambient.b += light.color.b * light.intensity;
        } else {
            selected.lights.push_back(&light);
        }
    }
    auto& lights = selected.lights;
    if (lights.size() > Renderer3D::MaxLights) {
        if (!warnedAboutLights) {
            Log::GetEngineLogger().Warning(
                "Renderer3D was given {} lights in a frame, more than the {} it can use; the "
                "farthest are left out",
                lights.size(), Renderer3D::MaxLights);
            warnedAboutLights = true;
        }
        std::ranges::stable_sort(lights, {},
                                 [&](const LightData* light) { return GetLightRank(*light, cameraPosition); });
        lights.resize(Renderer3D::MaxLights);
    }
    return selected;
}

LightingUniforms GetLightingUniforms(const Detail::SelectedLights& selected, Vector3 cameraPosition) {
    LightingUniforms uniforms{};
    uniforms.ambient = selected.ambient;
    uniforms.cameraPosition = {cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f};
    const auto& lights = selected.lights;
    uniforms.lightCount[0] = static_cast<std::uint32_t>(lights.size());
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const LightData& light = *lights[i];
        const Vector3 direction = Normalize(light.direction);
        const float outerCos = std::cos(light.outerConeAngle);
        // An inner angle at or past the outer one gives the cone a hard edge.
        const float innerCos = std::max(std::cos(light.innerConeAngle), outerCos + 0.0001f);
        const float type = light.type == LightType::Directional ? 0.0f : light.type == LightType::Point ? 1.0f : 2.0f;
        uniforms.lights[i] = {
            .positionAndType = {light.position.x, light.position.y, light.position.z, type},
            .directionAndRange = {direction.x, direction.y, direction.z, std::max(light.range, 0.0001f)},
            .color = {light.color.r * light.intensity, light.color.g * light.intensity, light.color.b * light.intensity,
                      1.0f},
            .cone = {outerCos, 1.0f / (innerCos - outerCos), selected.shadows[i], selected.shadowTexelSizes[i]},
        };
    }
    return uniforms;
}

} // namespace

Renderer3D::Renderer3D(GraphicsDevice& device, const Renderer3DConfig& config) : m_device(&device), m_config(config) {
    const auto vertexShader = device.CreateShader(Detail::Shaders::MeshVert);
    const auto fragmentShader = device.CreateShader(Detail::Shaders::MeshFrag);
    if (vertexShader && fragmentShader) {
        PipelineDesc desc{
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .vertexBuffers = {Mesh::GetVertexLayout()},
            .cullMode = CullMode::Back,
            .targetFormat = config.targetFormat,
            .depth = DepthState{.format = config.depthFormat},
            .sampleCount = config.sampleCount,
        };
        m_pipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::None;
        m_doubleSidedPipeline = device.CreatePipeline(desc);
    }

    const auto skyVertexShader = device.CreateShader(Detail::Shaders::SkyVert);
    const auto skyFragmentShader = device.CreateShader(Detail::Shaders::SkyFrag);
    if (skyVertexShader && skyFragmentShader) {
        m_skyPipeline = device.CreatePipeline({
            .vertexShader = skyVertexShader.get(),
            .fragmentShader = skyFragmentShader.get(),
            .targetFormat = config.targetFormat,
            .depth = DepthState{.format = config.depthFormat, .test = false, .write = false},
            .sampleCount = config.sampleCount,
        });
        m_skyCapturePipeline = device.CreatePipeline({
            .vertexShader = skyVertexShader.get(),
            .fragmentShader = skyFragmentShader.get(),
            .targetFormat = TextureFormat::Rgba16Float,
        });
    }
    m_skyPrefilter =
        FullscreenPass::Create(device, Detail::Shaders::SkyPrefilterFrag, {.targetFormat = TextureFormat::Rgba16Float});
    m_skyIrradiancePass = FullscreenPass::Create(device, Detail::Shaders::SkyIrradianceFrag,
                                                 {.targetFormat = TextureFormat::Rgba16Float});
    m_skySampler = device.CreateSampler();
    m_noSkyReflections = device.CreateTexture(
        {.width = 1, .height = 1, .format = TextureFormat::Rgba16Float, .type = TextureType::Cube});
    if (m_noSkyReflections) {
        device.UploadToTexture(*m_noSkyReflections, std::array<std::uint16_t, 6 * 4>{});
    }
    m_noSkyIrradiance = device.CreateTexture({.width = 9, .height = 1, .format = TextureFormat::Rgba16Float});
    if (m_noSkyIrradiance) {
        device.UploadToTexture(*m_noSkyIrradiance, std::array<std::uint16_t, 9 * 4>{});
    }

    const auto shadowVertexShader = device.CreateShader(Detail::Shaders::ShadowVert);
    const auto shadowFragmentShader = device.CreateShader(Detail::Shaders::ShadowFrag);
    if (shadowVertexShader && shadowFragmentShader) {
        PipelineDesc desc{
            .vertexShader = shadowVertexShader.get(),
            .fragmentShader = shadowFragmentShader.get(),
            .vertexBuffers = {{.stride = sizeof(MeshVertex),
                               .attributes = {{0, VertexFormat::Float3, offsetof(MeshVertex, position)}}}},
            .cullMode = CullMode::Back,
            .depthOnly = true,
            // Surfaces sloping away from the light cover the most depth per texel, and so need the most bias.
            .depth = DepthState{.format = TextureFormat::Depth32Float, .biasSlope = 1.5f},
        };
        m_shadowPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::None;
        m_doubleSidedShadowPipeline = device.CreatePipeline(desc);
    }

    m_sampler = device.CreateSampler(
        {.filter = config.filter, .addressMode = TextureAddressMode::Repeat, .maxAnisotropy = config.maxAnisotropy});
    m_shadowSampler = device.CreateSampler({.compare = CompareOp::LessOrEqual});
    m_noShadows =
        device.CreateTexture({.width = 1, .height = 1, .format = TextureFormat::Depth32Float, .renderTarget = true});

    // Untextured materials sample this, so one shader serves both kinds.
    m_whiteTexture = device.CreateTexture({.width = 1, .height = 1});
    if (m_whiteTexture) {
        device.UploadToTexture(*m_whiteTexture, std::array<std::uint8_t, 4>{255, 255, 255, 255});
    }

    if (!IsValid()) {
        Log::GetEngineLogger().Error("Renderer3D could not create its GPU resources and will not draw");
    }
}

Renderer3D::~Renderer3D() = default;

bool Renderer3D::IsValid() const {
    return m_pipeline && m_doubleSidedPipeline && m_skyPipeline && m_shadowPipeline && m_doubleSidedShadowPipeline &&
           m_sampler && m_shadowSampler && m_whiteTexture && m_noShadows && m_skyCapturePipeline && m_skyPrefilter &&
           m_skyIrradiancePass && m_skySampler && m_noSkyReflections && m_noSkyIrradiance;
}

void Renderer3D::Begin(RenderContext& context, const RenderPassDesc& target, const Camera3D& camera) {
    SPOT_ASSERT(!m_context, "Renderer3D::Begin() called twice without End()");
    SPOT_ASSERT(target.depthTarget && !target.depthOnly, "Renderer3D draws in passes with a color and a depth target");
    m_context = &context;
    m_target = target;
    const Vector2 viewSize =
        target.target
            ? Vector2{static_cast<float>(target.target->GetWidth()), static_cast<float>(target.target->GetHeight())}
            : Vector2{static_cast<float>(context.GetWidth()), static_cast<float>(context.GetHeight())};
    m_viewProjection = camera.GetViewProjection(viewSize);
    m_inverseSkyViewProjection = Inverse(camera.GetProjection(viewSize) * Matrix4::Rotation(Inverse(camera.rotation)));
    m_camera = camera;
    m_aspectRatio = viewSize.y > 0.0f ? viewSize.x / viewSize.y : 1.0f;
    m_commands.clear();
    m_lights.clear();
    m_sky.reset();
    m_fog.reset();
}

void Renderer3D::End() {
    SPOT_PROFILE_SCOPE("Renderer3D::End");
    SPOT_ASSERT(m_context, "Renderer3D::End() called without Begin()");
    m_stats = {};
    if (!IsValid()) {
        m_context->BeginRenderPass(m_target);
        m_context = nullptr;
        return;
    }

    std::optional<SkyLighting> skyLighting;
    m_skyLit = false;
    if (m_sky) {
        skyLighting = GetSkyLighting(*m_sky);
        if (m_sky->lightsScene) {
            if (skyLighting->sun) {
                LightData sun = *skyLighting->sun;
                sun.castsShadows = m_sky->sunCastsShadows;
                m_lights.push_back(sun);
            }
            // The sky lights from every direction, through its captured light; should that fail, it lights evenly.
            m_skyLit = UpdateSkyLighting(*m_sky, *skyLighting);
            if (!m_skyLit) {
                m_lights.push_back({.type = LightType::Ambient, .color = skyLighting->ambient});
            }
        }
    }
    Detail::SelectedLights lights = SelectLights(m_lights, m_camera.position, m_warnedAboutLights);

    // Surfaces are opaque and depth tested, so the order only matters for how often state changes.
    std::stable_sort(m_commands.begin(), m_commands.end(), [](const DrawCommand& a, const DrawCommand& b) {
        return std::tie(a.doubleSided, a.textures) < std::tie(b.doubleSided, b.textures);
    });
    Detail::ShadowUniforms shadows{};
    DrawShadows(lights, shadows);

    auto pass = m_context->BeginRenderPass(m_target);
    if (m_sky) {
        FlushSky(pass, *m_sky, *skyLighting);
    }
    Flush(pass, lights, shadows);
    m_context = nullptr;
}

void Renderer3D::DrawLight(const LightData& light) {
    SPOT_ASSERT(m_context, "Draw calls must come between Begin() and End()");
    m_lights.push_back(light);
}

void Renderer3D::DrawSky(const SkyData& sky) {
    SPOT_ASSERT(m_context, "Draw calls must come between Begin() and End()");
    m_sky = sky;
}

void Renderer3D::DrawFog(const FogData& fog) {
    SPOT_ASSERT(m_context, "Draw calls must come between Begin() and End()");
    m_fog = fog;
}

void Renderer3D::DrawMesh(const Mesh& mesh, const Material& material, const Matrix4& transform, bool castsShadows) {
    SPOT_ASSERT(m_context, "Draw calls must come between Begin() and End()");
    const BoundingSphere bounds = mesh.GetBounds();
    const Vector4 center = transform * Vector4{bounds.center.x, bounds.center.y, bounds.center.z, 1.0f};
    float scale = 0.0f;
    for (std::size_t i = 0; i < 3; ++i) {
        const Vector4& axis = transform.columns[i];
        scale = std::max(scale, Length(Vector3{axis.x, axis.y, axis.z}));
    }
    m_commands.push_back(
        {&mesh,
         {material.baseColorTexture.Get(), material.metallicRoughnessTexture.Get(), material.normalTexture.Get(),
          material.occlusionTexture.Get(), material.emissiveTexture.Get()},
         material.baseColor,
         material.metallic,
         material.roughness,
         material.normalTexture ? material.normalScale : 0.0f,
         material.occlusionStrength,
         material.emissive,
         material.doubleSided,
         material.worldTextureSize,
         transform,
         {{center.x, center.y, center.z}, bounds.radius * scale},
         castsShadows});
}

void Renderer3D::DrawModel(const Model& model, const Matrix4& transform) {
    // Parts whose mesh failed to load are skipped; without a material they are drawn plain white.
    static const Material DefaultMaterial;
    for (const auto& part : model.GetParts()) {
        if (part.mesh) {
            DrawMesh(*part.mesh, part.material ? *part.material : DefaultMaterial, transform);
        }
    }
}

void Renderer3D::DrawScene(Scene& scene) {
    SPOT_PROFILE_SCOPE("Renderer3D::DrawScene");
    static const Material DefaultMaterial;
    const Sky* sky = nullptr;
    scene.ForEachComponent<Sky>([&sky](const Sky& found) { sky = sky ? sky : &found; });
    // The sky lights the scene with its sun itself, once the air has dimmed it.
    const DirectionalLight* sun = sky ? sky->FindSun() : nullptr;
    scene.ForEachComponent<Light>([this, sun](const Light& light) {
        if (&light != sun) {
            DrawLight(light.GetLightData());
        }
    });
    if (sky) {
        DrawSky(sky->GetSkyData());
    }
    const Fog* fog = nullptr;
    scene.ForEachComponent<Fog>([&fog](const Fog& found) { fog = fog ? fog : &found; });
    if (fog) {
        DrawFog(fog->GetFogData());
    }
    scene.ForEachComponent<MeshRenderer>([this](const MeshRenderer& renderer) {
        if (renderer.GetMesh()) {
            DrawMesh(*renderer.GetMesh(), renderer.GetMaterial() ? *renderer.GetMaterial() : DefaultMaterial,
                     renderer.GetEntity().GetTransform().GetWorldMatrix(), renderer.IsCastingShadows());
        }
    });
}

namespace {

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
    };
}

} // namespace

bool Renderer3D::UpdateSkyLighting(const SkyData& sky, const SkyLighting& lighting) {
    if (m_capturedSky == sky) {
        return true;
    }
    if (!m_skyEnvironment) {
        m_skyEnvironment = m_device->CreateTexture({.width = SkyCubeSize,
                                                    .height = SkyCubeSize,
                                                    .format = TextureFormat::Rgba16Float,
                                                    .type = TextureType::Cube,
                                                    .renderTarget = true,
                                                    .mipmaps = true});
        m_skyReflections = m_device->CreateTexture({.width = SkyCubeSize,
                                                    .height = SkyCubeSize,
                                                    .format = TextureFormat::Rgba16Float,
                                                    .type = TextureType::Cube,
                                                    .renderTarget = true,
                                                    .mipmaps = true});
        m_skyIrradiance = m_device->CreateTexture(
            {.width = 9, .height = 1, .format = TextureFormat::Rgba16Float, .renderTarget = true});
    }
    if (!m_skyEnvironment || !m_skyReflections || !m_skyIrradiance) {
        return false;
    }

    // Each face is drawn with the sky's directions for its texels: clip-space x and y run along its right and up.
    const SkyUniforms uniforms = GetSkyUniforms(sky, lighting, true, true);
    for (std::uint32_t face = 0; face < 6; ++face) {
        const auto& [forward, right, up] = CubeFaces[face];
        Matrix4 directions;
        directions.columns = {ToVector4(right), ToVector4(up), Vector4{}, ToVector4(forward, 1.0f)};
        auto pass = m_context->BeginRenderPass({.target = m_skyEnvironment.get(), .targetLayer = face});
        pass.BindPipeline(*m_skyCapturePipeline);
        pass.PushVertexUniforms(0, directions);
        pass.PushFragmentUniforms(0, uniforms);
        pass.PushFragmentUniforms(1, GetFogUniforms(std::nullopt, {}));
        pass.Draw(3);
    }
    m_context->GenerateMipmaps(*m_skyEnvironment);

    const FullscreenInput environment{m_skyEnvironment.get(), m_skySampler.get()};
    const float levels = static_cast<float>(m_skyEnvironment->GetMipLevelCount());
    for (std::uint32_t level = 0; level < SkyReflectionLevels; ++level) {
        const float roughness = static_cast<float>(level) / static_cast<float>(SkyReflectionLevels - 1);
        for (std::uint32_t face = 0; face < 6; ++face) {
            const auto& [forward, right, up] = CubeFaces[face];
            auto pass = m_context->BeginRenderPass(
                {.target = m_skyReflections.get(), .targetLayer = face, .targetMipLevel = level});
            pass.PushFragmentUniforms(
                0, PrefilterUniforms{.forward = ToVector4(forward),
                                     .right = ToVector4(right),
                                     .up = ToVector4(up),
                                     .params = {roughness, static_cast<float>(SkyCubeSize), levels, 0.0f}});
            m_skyPrefilter->Draw(pass, {&environment, 1});
        }
    }

    {
        auto pass = m_context->BeginRenderPass({.target = m_skyIrradiance.get()});
        const float level = std::log2(static_cast<float>(SkyCubeSize / SkyIrradianceSamples));
        pass.PushFragmentUniforms(0, Vector4{static_cast<float>(SkyCubeSize), level, 0.0f, 0.0f});
        m_skyIrradiancePass->Draw(pass, {&environment, 1});
    }
    m_capturedSky = sky;
    ++m_stats.skyLightingUpdates;
    return true;
}

void Renderer3D::FlushSky(RenderPass& pass, const SkyData& sky, const SkyLighting& lighting) {
    pass.BindPipeline(*m_skyPipeline);
    pass.PushVertexUniforms(0, m_inverseSkyViewProjection);
    pass.PushFragmentUniforms(0, GetSkyUniforms(sky, lighting, m_config.hdr, false));
    pass.PushFragmentUniforms(1, GetFogUniforms(m_fog, m_camera.position));
    pass.Draw(3);
    ++m_stats.drawCalls;
    ++m_stats.triangleCount;
}

void Renderer3D::DrawShadows(Detail::SelectedLights& lights, Detail::ShadowUniforms& shadows) {
    if (!m_noShadowsCleared) {
        m_context->BeginRenderPass({.depthTarget = m_noShadows.get(), .clearDepth = 1.0f, .depthOnly = true});
        m_noShadowsCleared = true;
    }
    if (m_commands.empty()) {
        return;
    }
    if (m_config.shadowMapSize > 0) {
        for (std::size_t i = 0; i < lights.lights.size(); ++i) {
            const LightData& light = *lights.lights[i];
            if (light.type == LightType::Directional && light.castsShadows && light.direction != Vector3{}) {
                if (DrawCascades(light, shadows)) {
                    lights.shadows[i] = 0.0f;
                }
                break;
            }
        }
    }
    if (m_config.localShadowMapSize > 0) {
        DrawLocalShadows(lights, shadows);
    }
}

bool Renderer3D::DrawCascades(const LightData& light, Detail::ShadowUniforms& shadows) {
    const std::uint32_t count = std::clamp(m_config.shadowCascadeCount, 1u, Detail::MaxShadowCascades);
    const std::uint32_t size = m_config.shadowMapSize;
    const std::uint32_t columns = count > 1 ? 2 : 1;
    const std::uint32_t rows = count > 2 ? 2 : 1;
    if (!m_cascadeShadows || m_cascadeShadows->GetWidth() != columns * size ||
        m_cascadeShadows->GetHeight() != rows * size) {
        m_cascadeShadows = m_device->CreateTexture({.width = columns * size,
                                                    .height = rows * size,
                                                    .format = TextureFormat::Depth32Float,
                                                    .renderTarget = true});
        if (!m_cascadeShadows) {
            return false;
        }
    }

    const float nearPlane = m_camera.nearPlane;
    const auto ends = Detail::GetCascadeEnds(nearPlane, std::min(m_config.shadowDistance, m_camera.farPlane), count);
    const Vector3 forward = m_camera.GetForward();
    shadows.cascadeEnds = {ends[0], ends[1], ends[2], ends[3]};
    shadows.cameraForward = {forward.x, forward.y, forward.z, static_cast<float>(count)};
    shadows.cascadeLayout = {static_cast<float>(columns), static_cast<float>(rows),
                             1.0f / static_cast<float>(columns * size), 1.0f / static_cast<float>(rows * size)};

    auto pass =
        m_context->BeginRenderPass({.depthTarget = m_cascadeShadows.get(), .clearDepth = 1.0f, .depthOnly = true});
    std::array<float, Detail::MaxShadowCascades> texelSizes{};
    for (std::uint32_t cascade = 0; cascade < count; ++cascade) {
        const float start = cascade == 0 ? nearPlane : ends[cascade - 1];
        const Detail::ShadowView view =
            Detail::GetCascadeView(m_camera, m_aspectRatio, start, ends[cascade], light.direction, size);
        shadows.cascadeViews[cascade] = TileFromClip * view.viewProjection;
        texelSizes[cascade] = view.texelSize;
        pass.SetViewport({.x = static_cast<float>(cascade % columns * size),
                          .y = static_cast<float>(cascade / columns * size),
                          .width = static_cast<float>(size),
                          .height = static_cast<float>(size)});
        DrawShadowCasters(pass, view.viewProjection, true);
    }
    shadows.cascadeTexelSizes = {texelSizes[0], texelSizes[1], texelSizes[2], texelSizes[3]};
    return true;
}

void Renderer3D::DrawLocalShadows(Detail::SelectedLights& lights, Detail::ShadowUniforms& shadows) {
    // The lights that matter most to the view get tiles first; a spot light may still fit where a point light does not.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < lights.lights.size(); ++i) {
        const LightData& light = *lights.lights[i];
        if (light.type != LightType::Directional && light.castsShadows && light.range > 0.0f &&
            Detail::IsSphereInView(m_viewProjection, light.position, light.range)) {
            order.push_back(i);
        }
    }
    std::ranges::stable_sort(order, {},
                             [&](std::size_t i) { return GetLightRank(*lights.lights[i], m_camera.position); });
    std::size_t tileCount = 0;
    for (const std::size_t i : order) {
        const std::size_t tiles = lights.lights[i]->type == LightType::Point ? 6 : 1;
        if (tileCount + tiles <= MaxShadowTiles) {
            lights.shadows[i] = static_cast<float>(tileCount);
            tileCount += tiles;
        }
    }
    if (tileCount == 0) {
        return;
    }

    constexpr std::uint32_t Columns = 4;
    constexpr std::uint32_t Rows = MaxShadowTiles / Columns;
    const std::uint32_t size = m_config.localShadowMapSize;
    if (!m_localShadows || m_localShadows->GetWidth() != Columns * size) {
        m_localShadows = m_device->CreateTexture({.width = Columns * size,
                                                  .height = Rows * size,
                                                  .format = TextureFormat::Depth32Float,
                                                  .renderTarget = true});
        if (!m_localShadows) {
            lights.shadows.fill(-1.0f);
            return;
        }
    }
    shadows.tileLayout = {static_cast<float>(Columns), static_cast<float>(Rows),
                          1.0f / static_cast<float>(Columns * size), 1.0f / static_cast<float>(Rows * size)};

    auto pass =
        m_context->BeginRenderPass({.depthTarget = m_localShadows.get(), .clearDepth = 1.0f, .depthOnly = true});
    for (std::size_t i = 0; i < lights.lights.size(); ++i) {
        if (lights.shadows[i] < 0.0f) {
            continue;
        }
        const LightData& light = *lights.lights[i];
        const auto first = static_cast<std::uint32_t>(lights.shadows[i]);
        const std::uint32_t faces = light.type == LightType::Point ? 6 : 1;
        for (std::uint32_t face = 0; face < faces; ++face) {
            const Detail::ShadowView view = light.type == LightType::Point
                                                ? Detail::GetPointShadowView(light, face, size)
                                                : Detail::GetSpotShadowView(light, size);
            const std::uint32_t tile = first + face;
            shadows.tileViews[tile] = TileFromClip * view.viewProjection;
            lights.shadowTexelSizes[i] = view.texelSize;
            pass.SetViewport({.x = static_cast<float>(tile % Columns * size),
                              .y = static_cast<float>(tile / Columns * size),
                              .width = static_cast<float>(size),
                              .height = static_cast<float>(size)});
            DrawShadowCasters(pass, view.viewProjection, false);
        }
    }
}

void Renderer3D::DrawShadowCasters(RenderPass& pass, const Matrix4& viewProjection, bool ignoreNear) {
    pass.PushVertexUniforms(0, viewProjection);
    const Pipeline* boundPipeline = nullptr;
    for (const auto& command : m_commands) {
        if (!command.castsShadows ||
            !Detail::IsSphereInView(viewProjection, command.bounds.center, command.bounds.radius, ignoreNear)) {
            continue;
        }
        const Pipeline* pipeline = command.doubleSided ? m_doubleSidedShadowPipeline.get() : m_shadowPipeline.get();
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
        }
        pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
        pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
        pass.PushVertexUniforms(1, command.transform);
        pass.DrawIndexed(command.mesh->GetIndexCount());
        ++m_stats.shadowDrawCalls;
    }
}

void Renderer3D::Flush(RenderPass& pass, const Detail::SelectedLights& lights, const Detail::ShadowUniforms& shadows) {
    if (m_commands.empty()) {
        return;
    }

    pass.PushVertexUniforms(0, m_viewProjection);
    LightingUniforms lighting = GetLightingUniforms(lights, m_camera.position);
    lighting.skyLighting = {m_skyLit ? 1.0f : 0.0f, static_cast<float>(SkyReflectionLevels - 1), 0.0f, 0.0f};
    pass.PushFragmentUniforms(0, lighting);
    pass.PushFragmentUniforms(2, shadows);
    pass.PushFragmentUniforms(3, GetFogUniforms(m_fog, m_camera.position));
    pass.BindFragmentTexture(1, m_cascadeShadows ? *m_cascadeShadows : *m_noShadows, *m_shadowSampler);
    pass.BindFragmentTexture(2, m_localShadows ? *m_localShadows : *m_noShadows, *m_shadowSampler);
    pass.BindFragmentTexture(7, m_skyLit ? *m_skyReflections : *m_noSkyReflections, *m_skySampler);
    pass.BindFragmentTexture(8, m_skyLit ? *m_skyIrradiance : *m_noSkyIrradiance, *m_skySampler);

    const Pipeline* boundPipeline = nullptr;
    MaterialTextures boundTextures{};
    for (const auto& command : m_commands) {
        const Pipeline* pipeline = command.doubleSided ? m_doubleSidedPipeline.get() : m_pipeline.get();
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
        }
        // Missing textures are white, which leaves the values they multiply as they are.
        for (std::size_t i = 0; i < command.textures.size(); ++i) {
            const Texture* texture = command.textures[i] ? command.textures[i] : m_whiteTexture.get();
            if (texture != boundTextures[i]) {
                pass.BindFragmentTexture(i == 0 ? 0 : static_cast<std::uint32_t>(i + 2), *texture, *m_sampler);
                boundTextures[i] = texture;
            }
        }

        pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
        pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
        pass.PushVertexUniforms(1, ObjectUniforms{command.transform, Transpose(Inverse(command.transform))});
        pass.PushFragmentUniforms(1, MaterialUniforms{.baseColor = command.color,
                                                      .emissive = command.emissive,
                                                      .metallic = command.metallic,
                                                      .roughness = command.roughness,
                                                      .normalScale = command.normalScale,
                                                      .occlusionStrength = command.occlusionStrength,
                                                      .doubleSided = command.doubleSided ? 1.0f : 0.0f,
                                                      .worldTextureSize = command.worldTextureSize,
                                                      .padding = {}});
        pass.DrawIndexed(command.mesh->GetIndexCount());

        ++m_stats.drawCalls;
        m_stats.triangleCount += command.mesh->GetIndexCount() / 3;
    }
}

} // namespace Spot
