#include "graphics/renderer_3d.h"
#include "graphics/render_world.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/profiler.h"
#include "graphics/atmosphere.h"
#include "graphics/graphics_device.h"
#include "graphics/light_clusters.h"
#include "graphics/material.h"
#include "graphics/mesh.h"
#include "graphics/model.h"
#include "graphics/render_context.h"
#include "graphics/shadow_views.h"

#include "mesh.frag.h"
#include "mesh.vert.h"
#include "shadow.frag.h"
#include "shadow.vert.h"
#include "shadow_cutout.frag.h"
#include "skinned_mesh.vert.h"
#include "skinned_shadow.vert.h"
#include "sky.frag.h"
#include "sky.vert.h"
#include "sky_irradiance.frag.h"
#include "sky_prefilter.frag.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <tuple>
#include <unordered_map>

namespace Spot {

namespace {

// The view, and its planes, which batches whose instances the shaders cull test them against.
struct FrameUniforms {
    Matrix4 viewProjection;
    std::array<Vector4, 6> cullPlanes;
};

FrameUniforms MakeFrameUniforms(const Matrix4& viewProjection, bool ignoreNear = false) {
    return {viewProjection, Detail::Frustum(viewProjection, ignoreNear).GetPlanes()};
}

// The most instances of a batch of a render world kept on the GPU: views leave out batches outside them whole, and
// cull the instances of the others in the shaders, so smaller batches leave the GPU less to cull, larger ones the CPU
// fewer draws.
constexpr std::uint32_t WorldBatchSize = 128;

// A sphere around the spheres.
BoundingSphere Enclose(std::span<const BoundingSphere> spheres) {
    if (spheres.empty()) {
        return {};
    }
    Vector3 low = spheres[0].center;
    Vector3 high = low;
    for (const BoundingSphere& sphere : spheres) {
        const Vector3 r{sphere.radius, sphere.radius, sphere.radius};
        low = {std::min(low.x, sphere.center.x - r.x), std::min(low.y, sphere.center.y - r.y),
               std::min(low.z, sphere.center.z - r.z)};
        high = {std::max(high.x, sphere.center.x + r.x), std::max(high.y, sphere.center.y + r.y),
                std::max(high.z, sphere.center.z + r.z)};
    }
    const Vector3 center = (low + high) * 0.5f;
    float radius = 0.0f;
    for (const BoundingSphere& sphere : spheres) {
        radius = std::max(radius, Length(sphere.center - center) + sphere.radius);
    }
    return {center, radius};
}

// The inverse transpose of the model matrix's rotation and scale, which keeps normals perpendicular under non-uniform
// scale: its cofactors, signed by its determinant, since the shader normalizes the normals anyway.
Matrix4 GetNormalMatrix(const Matrix4& m) {
    const Vector3 x{m.columns[0].x, m.columns[0].y, m.columns[0].z};
    const Vector3 y{m.columns[1].x, m.columns[1].y, m.columns[1].z};
    const Vector3 z{m.columns[2].x, m.columns[2].y, m.columns[2].z};
    const Vector3 yz = Cross(y, z);
    const float sign = Dot(x, yz) < 0.0f ? -1.0f : 1.0f;
    const Vector3 a = yz * sign;
    const Vector3 b = Cross(z, x) * sign;
    const Vector3 c = Cross(x, y) * sign;
    Matrix4 normal;
    // Its last element is the sign, which flips the side of the tangent the bitangent is on in a mirror.
    normal.columns = {Vector4{a.x, a.y, a.z, 0.0f}, Vector4{b.x, b.y, b.z, 0.0f}, Vector4{c.x, c.y, c.z, 0.0f},
                      Vector4{0.0f, 0.0f, 0.0f, sign}};
    return normal;
}

// These mirror the shaders' buffers.
struct BatchUniforms {
    std::array<std::uint32_t, 4> firstInstance;
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
    // x is the number of directional lights; a whole row keeps the layout the same in every shader language.
    std::array<std::uint32_t, 4> lightCount;
    // 1 when the sky lights the scene, and the mip level of its reflections for a roughness of 1.
    Vector4 skyLighting;
    std::array<std::uint32_t, 4> clusterGrid;
    // The near plane's depth, the scale of the slices' logarithm, and the view's size in pixels.
    Vector4 clusterDepth;
    Vector4 viewForward;
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
    float alphaMode;
    float alphaCutoff;
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
    // Directional lights come first, as the shader goes through them for every pixel.
    std::ranges::stable_partition(lights, [](const LightData* light) { return light->type == LightType::Directional; });
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

std::vector<LightUniforms> GetLightUniforms(const Detail::SelectedLights& selected) {
    const auto& lights = selected.lights;
    std::vector<LightUniforms> uniforms(lights.size());
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const LightData& light = *lights[i];
        const Vector3 direction = Normalize(light.direction);
        const float outerCos = std::cos(light.outerConeAngle);
        // An inner angle at or past the outer one gives the cone a hard edge.
        const float innerCos = std::max(std::cos(light.innerConeAngle), outerCos + 0.0001f);
        const float type = light.type == LightType::Directional ? 0.0f : light.type == LightType::Point ? 1.0f : 2.0f;
        uniforms[i] = {
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
        // Transparent surfaces are tested against what is opaque, but do not hide what is drawn after them.
        desc.blendMode = BlendMode::Premultiplied;
        desc.depth = DepthState{.format = config.depthFormat, .write = false};
        m_doubleSidedTransparentPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Back;
        m_transparentPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Front;
        m_mirroredTransparentPipeline = device.CreatePipeline(desc);
        desc.blendMode = BlendMode::None;
        desc.depth = DepthState{.format = config.depthFormat};
        m_mirroredPipeline = device.CreatePipeline(desc);
    }

    const auto skinnedVertex = device.CreateShader(Detail::Shaders::SkinnedMeshVert);
    if (skinnedVertex && fragmentShader) {
        for (std::size_t i = 0; i < m_skinnedPipelines.size(); ++i) {
            m_skinnedPipelines[i] =
                device.CreatePipeline({.vertexShader = skinnedVertex.get(),
                                       .fragmentShader = fragmentShader.get(),
                                       .vertexBuffers = {Mesh::GetVertexLayout(), Mesh::GetSkinLayout()},
                                       .blendMode = i >= 3 ? BlendMode::Premultiplied : BlendMode::None,
                                       .cullMode = i % 3 == 0   ? CullMode::Back
                                                   : i % 3 == 1 ? CullMode::Front
                                                                : CullMode::None,
                                       .targetFormat = config.targetFormat,
                                       .depth = DepthState{.format = config.depthFormat, .write = i < 3},
                                       .sampleCount = config.sampleCount});
        }
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
    const auto cutoutShadowFragmentShader = device.CreateShader(Detail::Shaders::ShadowCutoutFrag);
    if (shadowVertexShader && shadowFragmentShader && cutoutShadowFragmentShader) {
        PipelineDesc desc{
            .vertexShader = shadowVertexShader.get(),
            .fragmentShader = shadowFragmentShader.get(),
            .vertexBuffers = {{.stride = sizeof(MeshVertex),
                               .attributes = {{0, VertexFormat::Float3, offsetof(MeshVertex, position)},
                                              {1, VertexFormat::Float2, offsetof(MeshVertex, uv)}}}},
            .cullMode = CullMode::Back,
            .depthOnly = true,
            // Surfaces sloping away from the light cover the most depth per texel, and so need the most bias.
            .depth = DepthState{.format = TextureFormat::Depth32Float, .biasSlope = 1.5f, .clamp = true},
        };
        m_shadowPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Front;
        m_mirroredShadowPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::None;
        m_doubleSidedShadowPipeline = device.CreatePipeline(desc);
        desc.fragmentShader = cutoutShadowFragmentShader.get();
        m_doubleSidedCutoutShadowPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Back;
        m_cutoutShadowPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Front;
        m_mirroredCutoutShadowPipeline = device.CreatePipeline(desc);
    }

    const auto skinnedShadow = device.CreateShader(Detail::Shaders::SkinnedShadowVert);
    if (skinnedShadow && shadowFragmentShader && cutoutShadowFragmentShader) {
        for (std::size_t i = 0; i < m_skinnedShadowPipelines.size(); ++i) {
            m_skinnedShadowPipelines[i] = device.CreatePipeline(
                {.vertexShader = skinnedShadow.get(),
                 .fragmentShader = i >= 3 ? cutoutShadowFragmentShader.get() : shadowFragmentShader.get(),
                 .vertexBuffers = {{.stride = sizeof(MeshVertex),
                                    .attributes = {{0, VertexFormat::Float3, offsetof(MeshVertex, position)},
                                                   {1, VertexFormat::Float2, offsetof(MeshVertex, uv)}}},
                                   Mesh::GetSkinLayout(2)},
                 .cullMode = i % 3 == 0   ? CullMode::Back
                             : i % 3 == 1 ? CullMode::Front
                                          : CullMode::None,
                 .depthOnly = true,
                 .depth = DepthState{.format = TextureFormat::Depth32Float, .biasSlope = 1.5f, .clamp = true}});
        }
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
    return std::ranges::all_of(m_skinnedPipelines, [](const auto& p) { return !!p; }) &&
           std::ranges::all_of(m_skinnedShadowPipelines, [](const auto& p) { return !!p; }) && m_pipeline &&
           m_doubleSidedPipeline && m_transparentPipeline && m_doubleSidedTransparentPipeline && m_mirroredPipeline &&
           m_mirroredTransparentPipeline && m_mirroredShadowPipeline && m_mirroredCutoutShadowPipeline &&
           m_skyPipeline && m_shadowPipeline && m_doubleSidedShadowPipeline && m_cutoutShadowPipeline &&
           m_doubleSidedCutoutShadowPipeline && m_sampler && m_shadowSampler && m_whiteTexture && m_noShadows &&
           m_skyCapturePipeline && m_skyPrefilter && m_skyIrradiancePass && m_skySampler && m_noSkyReflections &&
           m_noSkyIrradiance;
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
    m_viewSize = viewSize;
    m_aspectRatio = viewSize.y > 0.0f ? viewSize.x / viewSize.y : 1.0f;
    m_commands.clear();
    m_paletteData.clear();
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

    // Shadow passes cull against their own views; the view's pass leaves out what the camera cannot see.
    const Detail::Frustum view(m_viewProjection);
    for (DrawCommand& command : m_commands) {
        command.visible = view.Contains(command.bounds.center, command.bounds.radius);
        // A batch the shaders cull counts as seen whole, as far as the CPU can tell.
        (command.visible ? m_stats.visibleMeshes : m_stats.culledMeshes) += command.instanceCount;
    }
    AssignLooks();
    Profiler::Count("Visible meshes", m_stats.visibleMeshes);
    Profiler::Count("Culled meshes", m_stats.culledMeshes);

    {
        SPOT_PROFILE_SCOPE("Renderer3D::Sort");
        // Opaque and cutout surfaces are depth tested, so their order only matters for how often state changes.
        // Transparent ones come after them, from the farthest to the nearest, so each blends over what is behind it.
        // Only small keys move while sorting; the commands, which are large, then move once into their places.
        const Vector3 forward = m_camera.GetForward();
        const Vector3 eye = m_camera.position;
        m_sortEntries.clear();
        m_sortEntries.reserve(m_commands.size());
        for (std::size_t i = 0; i < m_commands.size(); ++i) {
            DrawCommand& c = m_commands[i];
            c.depth = Dot(c.bounds.center - eye, forward);
            const bool transparent = c.alphaMode == AlphaMode::Transparent;
            m_sortEntries.push_back({.transparent = transparent,
                                     .hidden = !c.visible,
                                     .doubleSided = c.doubleSided,
                                     .mirrored = c.mirrored,
                                     .alphaMode = c.alphaMode,
                                     .look = c.look,
                                     .mesh = c.mesh,
                                     .firstIndex = c.firstIndex,
                                     .depth = transparent ? LengthSquared(c.bounds.center - eye) : c.depth,
                                     .index = static_cast<std::uint32_t>(i)});
        }
        // Opaque commands in view come first, so those out of it do not split runs of instances. Then they go by
        // pipeline, then material, so state changes as rarely as it can, then mesh, so those that look the same are
        // side by side to be drawn as instances of one draw, and last from the nearest to the farthest, so what is in
        // front hides what is behind before it is shaded.
        std::stable_sort(m_sortEntries.begin(), m_sortEntries.end(), [](const SortEntry& a, const SortEntry& b) {
            if (a.transparent != b.transparent) {
                return !a.transparent;
            }
            if (a.transparent) {
                return a.depth > b.depth;
            }
            if (a.hidden != b.hidden) {
                return !a.hidden;
            }
            if (a.doubleSided != b.doubleSided) {
                return !a.doubleSided;
            }
            if (a.mirrored != b.mirrored) {
                return !a.mirrored;
            }
            if (a.alphaMode != b.alphaMode) {
                return a.alphaMode < b.alphaMode;
            }
            if (a.look != b.look) {
                return a.look < b.look;
            }
            if (a.mesh != b.mesh) {
                return std::less<const Mesh*>()(a.mesh, b.mesh);
            }
            if (a.firstIndex != b.firstIndex) {
                return a.firstIndex < b.firstIndex;
            }
            return a.depth < b.depth;
        });
        m_sortedCommands.clear();
        m_sortedCommands.reserve(m_commands.size());
        for (const SortEntry& entry : m_sortEntries) {
            m_sortedCommands.push_back(std::move(m_commands[entry.index]));
        }
        m_commands.swap(m_sortedCommands);
    }
    // Shadow views go through the casters in one order, kept for the frame.
    m_shadowOrder.clear();
    for (std::size_t i = 0; i < m_commands.size(); ++i) {
        // Transparent surfaces let light through, so they cast no shadow.
        if (m_commands[i].castsShadows && m_commands[i].alphaMode != AlphaMode::Transparent) {
            m_shadowOrder.push_back(static_cast<std::uint32_t>(i));
        }
    }
    const auto shadowKey = [this](std::uint32_t index) {
        const DrawCommand& c = m_commands[index];
        const bool cutout = c.alphaMode == AlphaMode::Cutout;
        return std::make_tuple(c.doubleSided, c.mirrored, cutout, cutout ? c.look : 0u, c.mesh, c.firstIndex);
    };
    std::ranges::stable_sort(m_shadowOrder,
                             [&](std::uint32_t a, std::uint32_t b) { return shadowKey(a) < shadowKey(b); });
    if (!UploadInstances()) {
        m_commands.clear();
    }
    Detail::ShadowUniforms shadows{};
    DrawShadows(lights, shadows);

    if (!UploadLights(lights)) {
        m_commands.clear();
    }
    auto pass = m_context->BeginRenderPass(m_target);
    if (m_sky) {
        FlushSky(pass, *m_sky, *skyLighting);
    }
    Flush(pass, lights, shadows);
    Profiler::Count("Draw calls", m_stats.drawCalls);
    Profiler::Count("Triangles", m_stats.triangleCount);
    Profiler::Count("Shadow draw calls", m_stats.shadowDrawCalls);
    m_context = nullptr;
    ++m_endCount;
    // Worlds not drawn for a while are let go, with their buffers.
    std::erase_if(m_worldCaches, [this](const auto& cache) { return m_endCount - cache->lastDrawn > 120; });
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

void Renderer3D::DrawMesh(const Mesh& mesh, const Material& material, const Matrix4& transform, bool castsShadows,
                          std::uint32_t submesh) {
    SPOT_ASSERT(m_context, "Draw calls must come between Begin() and End()");
    RenderMeshObject object;
    if (Detail::MakeMeshObject(
            {.mesh = &mesh, .transform = transform, .submesh = submesh, .castsShadows = castsShadows}, object)) {
        AddCommand(object, material);
    }
}

void Renderer3D::DrawWorld(const RenderWorld& world) {
    SPOT_ASSERT(m_context, "Draw calls must come between Begin() and End()");
    SPOT_PROFILE_SCOPE("Renderer3D::DrawWorld");
    static const Material DefaultMaterial;
    const std::span<const RenderMeshObject> objects = world.GetMeshes();
    const auto sees = [this](std::uint32_t layer) { return ((m_camera.cullingMask >> layer) & 1u) != 0; };
    WorldCache* cache = UpdateWorldCache(world);
    if (!cache) {
        for (const RenderMeshObject& object : objects) {
            if (object.desc.mesh && sees(object.desc.layer)) {
                AddCommand(object, object.desc.material ? *object.desc.material : DefaultMaterial);
            }
        }
        return;
    }
    for (const WorldBatch& batch : cache->batches) {
        if (!sees(batch.layer)) {
            continue;
        }
        const Material& material = batch.material ? *batch.material : DefaultMaterial;
        // Transparent surfaces are drawn one by one, from the farthest, so each blends over what is behind it.
        if (material.alphaMode == AlphaMode::Transparent) {
            for (std::uint32_t i = batch.first; i < batch.first + batch.count; ++i) {
                AddCommand(objects[cache->objects[i]], material);
            }
            continue;
        }
        const RenderMeshObject proxy{.desc = {.mesh = batch.mesh, .castsShadows = batch.castsShadows},
                                     .bounds = batch.bounds,
                                     .mirrored = batch.mirrored,
                                     .firstIndex = batch.firstIndex,
                                     .indexCount = batch.indexCount};
        AddCommand(proxy, material);
        DrawCommand& command = m_commands.back();
        command.instanceBuffer = cache->instances.get();
        command.instanceData = cache->instanceData.data();
        command.instanceFirst = batch.first;
        command.instanceCount = batch.count;
    }
}

Renderer3D::WorldCache* Renderer3D::UpdateWorldCache(const RenderWorld& world) {
    SPOT_PROFILE_SCOPE("Renderer3D::UpdateWorldCache");
    auto found =
        std::ranges::find_if(m_worldCaches, [&](const auto& cache) { return cache->worldId == world.GetId(); });
    if (found == m_worldCaches.end()) {
        m_worldCaches.push_back(std::make_unique<WorldCache>());
        m_worldCaches.back()->worldId = world.GetId();
        found = m_worldCaches.end() - 1;
    }
    WorldCache& cache = **found;
    cache.lastDrawn = m_endCount;
    const std::span<const RenderMeshObject> objects = world.GetMeshes();
    const auto makeInstance = [](const RenderMeshObject& object) {
        return InstanceData{object.desc.transform,
                            GetNormalMatrix(object.desc.transform),
                            {},
                            ToVector4(object.bounds.center, object.bounds.radius)};
    };

    if (!cache.built || cache.structureVersion != world.GetStructureVersion()) {
        // Objects drawn the same way go side by side, as one batch.
        cache.objects.clear();
        for (std::uint32_t i = 0; i < objects.size(); ++i) {
            if (objects[i].desc.mesh && objects[i].indexCount > 0) {
                cache.objects.push_back(i);
            }
        }
        const auto key = [&](std::uint32_t index) {
            const RenderMeshObject& o = objects[index];
            return std::make_tuple(o.desc.layer, reinterpret_cast<std::uintptr_t>(o.desc.material),
                                   reinterpret_cast<std::uintptr_t>(o.desc.mesh), o.firstIndex, o.indexCount,
                                   o.mirrored, o.desc.castsShadows);
        };
        // Within those, near ones go together, so each batch covers a small part of the world that views can leave
        // out whole: they are ordered along a Morton curve through the world's bounds, and cut every BatchSize.
        Vector3 low{};
        Vector3 high{};
        for (std::size_t i = 0; i < cache.objects.size(); ++i) {
            const Vector3 c = objects[cache.objects[i]].bounds.center;
            low = i == 0 ? c : Vector3{std::min(low.x, c.x), std::min(low.y, c.y), std::min(low.z, c.z)};
            high = i == 0 ? c : Vector3{std::max(high.x, c.x), std::max(high.y, c.y), std::max(high.z, c.z)};
        }
        const auto spread = [](std::uint32_t v) {
            std::uint32_t x = v & 0x3FFu;
            x = (x | (x << 16)) & 0x030000FFu;
            x = (x | (x << 8)) & 0x0300F00Fu;
            x = (x | (x << 4)) & 0x030C30C3u;
            x = (x | (x << 2)) & 0x09249249u;
            return x;
        };
        const auto morton = [&](std::uint32_t index) {
            const Vector3 c = objects[index].bounds.center;
            const auto cell = [](float value, float from, float to) {
                const float t = to > from ? (value - from) / (to - from) : 0.0f;
                return static_cast<std::uint32_t>(std::clamp(t, 0.0f, 1.0f) * 1023.0f);
            };
            return spread(cell(c.x, low.x, high.x)) | (spread(cell(c.y, low.y, high.y)) << 1) |
                   (spread(cell(c.z, low.z, high.z)) << 2);
        };
        std::vector<std::uint32_t> codes(objects.size());
        for (const std::uint32_t index : cache.objects) {
            codes[index] = morton(index);
        }
        std::ranges::sort(cache.objects, [&](std::uint32_t a, std::uint32_t b) {
            const auto ka = key(a);
            const auto kb = key(b);
            return ka != kb ? ka < kb : codes[a] < codes[b];
        });
        cache.slots.assign(objects.size(), ~0u);
        cache.instanceData.clear();
        cache.instanceData.reserve(cache.objects.size());
        cache.batchOfInstance.resize(cache.objects.size());
        cache.batches.clear();
        for (std::uint32_t i = 0; i < cache.objects.size(); ++i) {
            const std::uint32_t index = cache.objects[i];
            const RenderMeshObject& object = objects[index];
            cache.slots[index] = i;
            cache.instanceData.push_back(makeInstance(object));
            if (cache.batches.empty() || cache.batches.back().count == WorldBatchSize ||
                key(cache.objects[cache.batches.back().first]) != key(index)) {
                cache.batches.push_back({.mesh = object.desc.mesh,
                                         .material = object.desc.material,
                                         .firstIndex = object.firstIndex,
                                         .indexCount = object.indexCount,
                                         .mirrored = object.mirrored,
                                         .castsShadows = object.desc.castsShadows,
                                         .layer = object.desc.layer,
                                         .first = i,
                                         .count = 0,
                                         .bounds = {},
                                         .boundsChanged = true});
            }
            ++cache.batches.back().count;
            cache.batchOfInstance[i] = static_cast<std::uint32_t>(cache.batches.size() - 1);
        }
        const auto size =
            static_cast<std::uint32_t>(std::max<std::size_t>(cache.instanceData.size(), 1) * sizeof(InstanceData));
        if (!cache.instances || cache.instances->GetSize() < size) {
            std::uint32_t capacity = 64 * sizeof(InstanceData);
            while (capacity < size) {
                capacity *= 2;
            }
            cache.instances = m_device->CreateBuffer({.usage = BufferUsage::Storage, .size = capacity});
        }
        if (!cache.instances) {
            cache.built = false;
            return nullptr;
        }
        if (!cache.instanceData.empty()) {
            m_device->UploadToBuffer(*cache.instances, std::span<const InstanceData>(cache.instanceData), 0,
                                     UploadMode::Discard);
        }
        cache.built = true;
        cache.structureVersion = world.GetStructureVersion();
    } else if (cache.changeCount != world.GetChangeCount()) {
        // Only the instances that moved go up again, in runs of neighbors.
        const std::span<const std::uint64_t> stamps = world.GetChangeStamps();
        std::vector<std::uint32_t> changed;
        for (std::uint32_t i = 0; i < stamps.size(); ++i) {
            if (stamps[i] > cache.changeCount && cache.slots[i] != ~0u) {
                const std::uint32_t slot = cache.slots[i];
                cache.instanceData[slot] = makeInstance(objects[i]);
                cache.batches[cache.batchOfInstance[slot]].boundsChanged = true;
                changed.push_back(slot);
            }
        }
        std::ranges::sort(changed);
        constexpr std::size_t MaxRuns = 64;
        std::size_t runs = 0;
        for (std::size_t i = 0; i < changed.size() && runs <= MaxRuns; ++i) {
            runs += i == 0 || changed[i] != changed[i - 1] + 1 ? 1 : 0;
        }
        if (runs > MaxRuns) {
            m_device->UploadToBuffer(*cache.instances, std::span<const InstanceData>(cache.instanceData), 0,
                                     UploadMode::Preserve);
        } else {
            for (std::size_t first = 0; first < changed.size();) {
                std::size_t end = first + 1;
                while (end < changed.size() && changed[end] == changed[end - 1] + 1) {
                    ++end;
                }
                m_device->UploadToBuffer(
                    *cache.instances,
                    std::span<const InstanceData>(cache.instanceData.data() + changed[first], end - first),
                    static_cast<std::uint32_t>(changed[first] * sizeof(InstanceData)), UploadMode::Preserve);
                first = end;
            }
        }
    }
    cache.changeCount = world.GetChangeCount();

    std::vector<BoundingSphere> spheres;
    for (WorldBatch& batch : cache.batches) {
        if (!batch.boundsChanged) {
            continue;
        }
        spheres.clear();
        for (std::uint32_t i = batch.first; i < batch.first + batch.count; ++i) {
            spheres.push_back(objects[cache.objects[i]].bounds);
        }
        batch.bounds = Enclose(spheres);
        batch.boundsChanged = false;
    }
    return &cache;
}

void Renderer3D::AddCommand(const RenderMeshObject& object, const Material& material) {
    m_commands.push_back(
        {object.desc.mesh,
         {material.baseColorTexture.Get(), material.metallicRoughnessTexture.Get(), material.normalTexture.Get(),
          material.occlusionTexture.Get(), material.emissiveTexture.Get()},
         material.baseColor,
         material.metallic,
         material.roughness,
         material.normalTexture ? material.normalScale : 0.0f,
         material.occlusionStrength,
         material.emissive,
         material.alphaMode,
         material.alphaCutoff,
         material.doubleSided,
         material.worldTextureSize,
         object.desc.transform,
         object.bounds,
         object.desc.castsShadows});
    DrawCommand& command = m_commands.back();
    command.firstIndex = object.firstIndex;
    command.indexCount = object.indexCount;
    command.mirrored = object.mirrored;
}

void Renderer3D::DrawSkinnedMesh(const Mesh& mesh, const Material& material, std::span<const Matrix4> palette,
                                 const Matrix4& transform, float boundsPadding, bool castsShadows,
                                 std::uint32_t submesh) {
    if (!mesh.GetSkinBuffer() || palette.size() < mesh.GetSkinJointCount()) {
        Log::GetEngineLogger().Warning("Skinned mesh has no valid palette or skin stream");
        return;
    }
    const auto before = m_commands.size();
    DrawMesh(mesh, material, transform, castsShadows, submesh);
    if (m_commands.size() == before) {
        return;
    }
    auto& command = m_commands.back();
    command.skinOffset = static_cast<std::uint32_t>(m_paletteData.size());
    command.skinCount = static_cast<std::uint32_t>(palette.size());
    m_paletteData.insert(m_paletteData.end(), palette.begin(), palette.end());
    float scale = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& axis = transform.columns[i];
        scale = std::max(scale, Length(Vector3{axis.x, axis.y, axis.z}));
    }
    command.bounds.radius += std::max(boundsPadding, 0.0f) * scale;
}

void Renderer3D::DrawModel(const Model& model, const Matrix4& transform) {
    // Parts whose mesh failed to load are skipped; without a material they are drawn plain white.
    static const Material DefaultMaterial;
    for (const auto& part : model.GetParts()) {
        if (part.mesh) {
            DrawMesh(*part.mesh, part.material ? *part.material : DefaultMaterial, transform * part.transform, true,
                     part.submesh);
        }
    }
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
    SPOT_PROFILE_SCOPE("Renderer3D::UpdateSkyLighting");
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
    SPOT_PROFILE_SCOPE("Renderer3D::DrawShadows");
    // Views record their draws before their casters reach the GPU, which takes one upload at the end. The buffer has
    // room for every view to draw every caster, so it never has to grow while draws refer to it, and starts in fresh
    // memory, so views drawn by an earlier End() in the frame keep their own.
    m_frameCasters.clear();
    std::size_t shadowInstances = 0;
    for (const std::uint32_t index : m_shadowOrder) {
        shadowInstances += m_commands[index].instanceCount;
    }
    const std::size_t maxCasters = (Detail::MaxShadowCascades + MaxShadowTiles) * shadowInstances + 1;
    if (!m_shadowCasters || m_shadowCasters->GetSize() < maxCasters * sizeof(std::uint32_t)) {
        m_shadowCasters = m_device->CreateBuffer(
            {.usage = BufferUsage::Storage, .size = static_cast<std::uint32_t>(maxCasters * sizeof(std::uint32_t))});
    }
    if (!m_shadowCasters) {
        return;
    }
    m_device->UploadToBuffer(*m_shadowCasters, std::array<std::uint32_t, 1>{}, 0, UploadMode::Discard);
    DrawShadowViews(lights, shadows);
    // Smaller than the buffer, so it fills the memory the draws were recorded with instead of moving to new memory.
    if (!m_frameCasters.empty()) {
        m_device->UploadToBuffer(*m_shadowCasters, m_frameCasters, 0, UploadMode::Preserve);
    }
}

void Renderer3D::DrawShadowViews(Detail::SelectedLights& lights, Detail::ShadowUniforms& shadows) {
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

namespace {

// Whether the two commands draw the same mesh the same way, so they can be instances of one draw.
bool HaveSameLook(const auto& a, const auto& b) {
    return !a.instanceBuffer && !b.instanceBuffer && a.mesh == b.mesh && a.firstIndex == b.firstIndex &&
           a.indexCount == b.indexCount && a.look == b.look && a.mirrored == b.mirrored &&
           (a.skinCount != 0) == (b.skinCount != 0);
}

// Whether the two commands cast the same shadow but for their transforms: depth alone, or cut out the same way.
bool HaveSameShadow(const auto& a, const auto& b) {
    if (a.instanceBuffer || b.instanceBuffer || (a.skinCount != 0) != (b.skinCount != 0) || a.mesh != b.mesh ||
        a.firstIndex != b.firstIndex || a.indexCount != b.indexCount || a.doubleSided != b.doubleSided ||
        a.mirrored != b.mirrored || a.alphaMode != b.alphaMode) {
        return false;
    }
    return a.alphaMode != AlphaMode::Cutout || a.look == b.look;
}

} // namespace

namespace {

// Uploads the elements, growing the buffer to the next power of two when they do not fit. Buffers are never empty,
// so they can be bound when there is nothing to put in them.
template <typename T>
bool UploadStorage(GraphicsDevice& device, std::unique_ptr<Buffer>& buffer, const std::vector<T>& elements) {
    const auto size = static_cast<std::uint32_t>(std::max<std::size_t>(elements.size(), 1) * sizeof(T));
    if (!buffer || buffer->GetSize() < size) {
        std::uint32_t capacity = 64 * sizeof(T);
        while (capacity < size) {
            capacity *= 2;
        }
        buffer = device.CreateBuffer({.usage = BufferUsage::Storage, .size = capacity});
        if (!buffer) {
            return false;
        }
    }
    // Discarded, so views drawn earlier in the frame keep what they were drawn with.
    if (!elements.empty()) {
        device.UploadToBuffer(*buffer, elements, 0, UploadMode::Discard);
    }
    return true;
}

} // namespace

bool Renderer3D::UploadLights(const Detail::SelectedLights& selected) {
    SPOT_PROFILE_SCOPE("Renderer3D::UploadLights");
    const std::vector<LightUniforms> lights = GetLightUniforms(selected);
    std::vector<Detail::ClusterLight> clustered;
    m_directionalLightCount = 0;
    for (std::size_t i = 0; i < selected.lights.size(); ++i) {
        const LightData& light = *selected.lights[i];
        if (light.type == LightType::Directional) {
            ++m_directionalLightCount;
        } else {
            clustered.push_back({light.position, std::max(light.range, 0.0001f), static_cast<std::uint32_t>(i)});
        }
    }
    const Detail::LightClusters clusters = Detail::BuildLightClusters(m_camera, m_viewSize, clustered);
    m_clusterNear = clusters.nearDepth;
    m_clusterScale = clusters.sliceScale;
    m_stats.lights = static_cast<std::uint32_t>(lights.size());
    Profiler::Count("Lights", static_cast<double>(lights.size()));
    return UploadStorage(*m_device, m_lightBuffer, lights) &&
           UploadStorage(*m_device, m_clusterBuffer, clusters.clusters) &&
           UploadStorage(*m_device, m_lightIndexBuffer, clusters.lightIndices);
}

void Renderer3D::AssignLooks() {
    // Materials look the same when their textures and every value match, as copies of a material do.
    struct Look {
        MaterialTextures textures;
        std::array<float, 15> values;
        bool operator==(const Look&) const = default;
    };
    struct LookHash {
        std::size_t operator()(const Look& look) const {
            std::size_t hash = 0;
            const auto mix = [&hash](std::size_t value) {
                hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
            };
            for (const Texture* texture : look.textures) {
                mix(std::hash<const Texture*>()(texture));
            }
            for (const float value : look.values) {
                mix(std::hash<float>()(value));
            }
            return hash;
        }
    };
    std::unordered_map<Look, std::uint32_t, LookHash> looks;
    for (DrawCommand& c : m_commands) {
        const Look look{c.textures,
                        {c.color.r, c.color.g, c.color.b, c.color.a, c.metallic, c.roughness, c.normalScale,
                         c.occlusionStrength, c.emissive.r, c.emissive.g, c.emissive.b, c.alphaCutoff,
                         c.worldTextureSize, static_cast<float>(c.alphaMode), c.doubleSided ? 1.0f : 0.0f}};
        c.look = looks.try_emplace(look, static_cast<std::uint32_t>(looks.size())).first->second;
    }
}

bool Renderer3D::UploadInstances() {
    SPOT_PROFILE_SCOPE("Renderer3D::UploadInstances");
    if (m_commands.empty()) {
        return true;
    }
    std::vector<InstanceData> instances;
    instances.reserve(m_commands.size());
    for (const DrawCommand& command : m_commands) {
        instances.push_back({command.transform,
                             GetNormalMatrix(command.transform),
                             {command.skinOffset, command.skinCount, 0, 0},
                             ToVector4(command.bounds.center, command.bounds.radius)});
    }
    return UploadStorage(*m_device, m_instances, instances) && UploadStorage(*m_device, m_palettes, m_paletteData);
}

void Renderer3D::DrawShadowCasters(RenderPass& pass, const Matrix4& viewProjection, bool ignoreNear) {
    SPOT_PROFILE_SCOPE("Renderer3D::DrawShadowCasters");
    if (m_commands.empty()) {
        return;
    }
    // The view's casters, gathered so those that cast the same shadow are side by side, whatever the view leaves out
    // between them, and read through their indices. Transparent surfaces let light through, so they cast no shadow.
    // The view's casters go after those of the views drawn before, in one list that DrawShadows() uploads once.
    const Detail::Frustum frustum(viewProjection, ignoreNear);
    const std::size_t viewStart = m_frameCasters.size();
    bool worldCasters = false;
    for (const std::uint32_t index : m_shadowOrder) {
        const DrawCommand& command = m_commands[index];
        if (frustum.Contains(command.bounds.center, command.bounds.radius)) {
            if (command.instanceBuffer) {
                worldCasters = true;
            } else {
                m_frameCasters.push_back(index);
            }
        }
    }
    const std::span<const std::uint32_t> casters(m_frameCasters.data() + viewStart, m_frameCasters.size() - viewStart);
    if (casters.empty() && !worldCasters) {
        return;
    }
    pass.PushVertexUniforms(0, MakeFrameUniforms(viewProjection, ignoreNear));
    pass.BindVertexStorageBuffer(0, *m_instances);
    pass.BindVertexStorageBuffer(1, *m_shadowCasters);
    const Pipeline* boundPipeline = nullptr;
    const Texture* boundTexture = nullptr;
    for (std::size_t first = 0; first < casters.size();) {
        const DrawCommand& command = m_commands[casters[first]];
        std::size_t end = first + 1;
        while (end < casters.size() && HaveSameShadow(command, m_commands[casters[end]])) {
            ++end;
        }
        const bool cutout = command.alphaMode == AlphaMode::Cutout;
        const Pipeline* pipeline = GetShadowPipeline(command);
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
        }
        // Cutout shadows follow the mesh's UVs, even for textures projected from world space.
        if (cutout) {
            const Texture* texture = command.textures[0] ? command.textures[0] : m_whiteTexture.get();
            if (texture != boundTexture) {
                pass.BindFragmentTexture(0, *texture, *m_sampler);
                boundTexture = texture;
            }
            pass.PushFragmentUniforms(0, Vector4{command.color.a, command.alphaCutoff, 0.0f, 0.0f});
        }
        if (command.skinCount) {
            pass.BindVertexStorageBuffer(2, *m_palettes);
            pass.BindVertexBuffer(1, *command.mesh->GetSkinBuffer());
        }
        pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
        pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
        pass.PushVertexUniforms(1, BatchUniforms{{static_cast<std::uint32_t>(viewStart + first), 0, 0, 0}});
        pass.DrawIndexed(command.indexCount, static_cast<std::uint32_t>(end - first), command.firstIndex);
        ++m_stats.shadowDrawCalls;
        first = end;
    }
    if (!worldCasters) {
        return;
    }
    // Batches of worlds the view reaches are culled instance by instance into the casters, as those cast into small
    // views, such as a point light's, are mostly out of them.
    for (const std::uint32_t index : m_shadowOrder) {
        const DrawCommand& command = m_commands[index];
        if (!command.instanceBuffer || !frustum.Contains(command.bounds.center, command.bounds.radius)) {
            continue;
        }
        const auto batchStart = static_cast<std::uint32_t>(m_frameCasters.size());
        for (std::uint32_t i = command.instanceFirst; i < command.instanceFirst + command.instanceCount; ++i) {
            const Vector4& bounds = command.instanceData[i].bounds;
            if (frustum.Contains({bounds.x, bounds.y, bounds.z}, bounds.w)) {
                m_frameCasters.push_back(i);
            }
        }
        const auto visible = static_cast<std::uint32_t>(m_frameCasters.size()) - batchStart;
        if (visible == 0) {
            continue;
        }
        const Pipeline* pipeline = GetShadowPipeline(command);
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
        }
        if (command.alphaMode == AlphaMode::Cutout) {
            const Texture* texture = command.textures[0] ? command.textures[0] : m_whiteTexture.get();
            if (texture != boundTexture) {
                pass.BindFragmentTexture(0, *texture, *m_sampler);
                boundTexture = texture;
            }
            pass.PushFragmentUniforms(0, Vector4{command.color.a, command.alphaCutoff, 0.0f, 0.0f});
        }
        pass.BindVertexStorageBuffer(0, *command.instanceBuffer);
        pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
        pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
        pass.PushVertexUniforms(1, BatchUniforms{{batchStart, 0, 0, 0}});
        pass.DrawIndexed(command.indexCount, visible, command.firstIndex);
        ++m_stats.shadowDrawCalls;
    }
}

const Pipeline* Renderer3D::GetPipeline(const DrawCommand& command) const {
    if (command.skinCount) {
        const auto variant = (command.alphaMode == AlphaMode::Transparent ? 3u : 0u) + (command.doubleSided ? 2u
                                                                                        : command.mirrored  ? 1u
                                                                                                            : 0u);
        return m_skinnedPipelines[variant].get();
    }
    if (command.alphaMode == AlphaMode::Transparent) {
        return command.doubleSided ? m_doubleSidedTransparentPipeline.get()
               : command.mirrored  ? m_mirroredTransparentPipeline.get()
                                   : m_transparentPipeline.get();
    }
    return command.doubleSided ? m_doubleSidedPipeline.get()
           : command.mirrored  ? m_mirroredPipeline.get()
                               : m_pipeline.get();
}

const Pipeline* Renderer3D::GetShadowPipeline(const DrawCommand& command) const {
    if (command.skinCount) {
        const auto variant = (command.alphaMode == AlphaMode::Cutout ? 3u : 0u) + (command.doubleSided ? 2u
                                                                                   : command.mirrored  ? 1u
                                                                                                       : 0u);
        return m_skinnedShadowPipelines[variant].get();
    }
    if (command.alphaMode == AlphaMode::Cutout) {
        return command.doubleSided ? m_doubleSidedCutoutShadowPipeline.get()
               : command.mirrored  ? m_mirroredCutoutShadowPipeline.get()
                                   : m_cutoutShadowPipeline.get();
    }
    return command.doubleSided ? m_doubleSidedShadowPipeline.get()
           : command.mirrored  ? m_mirroredShadowPipeline.get()
                               : m_shadowPipeline.get();
}

void Renderer3D::Flush(RenderPass& pass, const Detail::SelectedLights& lights, const Detail::ShadowUniforms& shadows) {
    SPOT_PROFILE_SCOPE("Renderer3D::Flush");
    if (m_commands.empty()) {
        return;
    }

    pass.PushVertexUniforms(0, MakeFrameUniforms(m_viewProjection));
    const Vector3 forward = m_camera.GetForward();
    const LightingUniforms lighting{
        .ambient = lights.ambient,
        .cameraPosition = ToVector4(m_camera.position, 1.0f),
        .lightCount = {m_directionalLightCount, 0, 0, 0},
        .skyLighting = {m_skyLit ? 1.0f : 0.0f, static_cast<float>(SkyReflectionLevels - 1), 0.0f, 0.0f},
        .clusterGrid = {Detail::ClusterTilesX, Detail::ClusterTilesY, Detail::ClusterSlices, 0},
        .clusterDepth = {m_clusterNear, m_clusterScale, m_viewSize.x, m_viewSize.y},
        .viewForward = ToVector4(forward),
    };
    pass.PushFragmentUniforms(0, lighting);
    pass.BindFragmentStorageBuffer(0, *m_lightBuffer);
    pass.BindFragmentStorageBuffer(1, *m_clusterBuffer);
    pass.BindFragmentStorageBuffer(2, *m_lightIndexBuffer);
    pass.PushFragmentUniforms(2, shadows);
    pass.PushFragmentUniforms(3, GetFogUniforms(m_fog, m_camera.position));
    pass.BindFragmentTexture(1, m_cascadeShadows ? *m_cascadeShadows : *m_noShadows, *m_shadowSampler);
    pass.BindFragmentTexture(2, m_localShadows ? *m_localShadows : *m_noShadows, *m_shadowSampler);
    pass.BindFragmentTexture(7, m_skyLit ? *m_skyReflections : *m_noSkyReflections, *m_skySampler);
    pass.BindFragmentTexture(8, m_skyLit ? *m_skyIrradiance : *m_noSkyIrradiance, *m_skySampler);

    pass.BindVertexStorageBuffer(0, *m_instances);
    const Buffer* boundInstances = m_instances.get();
    const Pipeline* boundPipeline = nullptr;
    MaterialTextures boundTextures{};
    const Mesh* boundMesh = nullptr;
    std::optional<MaterialUniforms> boundMaterial;
    for (std::size_t first = 0; first < m_commands.size();) {
        const DrawCommand& command = m_commands[first];
        if (!command.visible) {
            ++first;
            continue;
        }
        // Batches are runs of commands that look the same, which sorting gathers, all in view.
        std::size_t end = first + 1;
        while (end < m_commands.size() && m_commands[end].visible && HaveSameLook(command, m_commands[end])) {
            ++end;
        }
        const auto instanceCount = static_cast<std::uint32_t>(end - first);
        const Pipeline* pipeline = GetPipeline(command);
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
            ++m_stats.pipelineChanges;
        }
        if (command.skinCount) {
            pass.BindVertexStorageBuffer(1, *m_palettes);
            pass.BindVertexBuffer(1, *command.mesh->GetSkinBuffer());
        }
        // Missing textures are white, which leaves the values they multiply as they are.
        bool materialChanged = false;
        for (std::size_t i = 0; i < command.textures.size(); ++i) {
            const Texture* texture = command.textures[i] ? command.textures[i] : m_whiteTexture.get();
            if (texture != boundTextures[i]) {
                pass.BindFragmentTexture(i == 0 ? 0 : static_cast<std::uint32_t>(i + 2), *texture, *m_sampler);
                boundTextures[i] = texture;
                materialChanged = true;
            }
        }
        const MaterialUniforms material{.baseColor = command.color,
                                        .emissive = command.emissive,
                                        .metallic = command.metallic,
                                        .roughness = command.roughness,
                                        .normalScale = command.normalScale,
                                        .occlusionStrength = command.occlusionStrength,
                                        .doubleSided = command.doubleSided ? 1.0f : 0.0f,
                                        .worldTextureSize = command.worldTextureSize,
                                        .alphaMode = static_cast<float>(command.alphaMode),
                                        .alphaCutoff = command.alphaCutoff};
        if (!boundMaterial || std::memcmp(&*boundMaterial, &material, sizeof(MaterialUniforms)) != 0) {
            pass.PushFragmentUniforms(1, material);
            boundMaterial = material;
            materialChanged = true;
        }
        m_stats.materialChanges += materialChanged ? 1 : 0;
        if (command.mesh != boundMesh) {
            if (command.skinCount) {
                pass.BindVertexStorageBuffer(1, *m_palettes);
                pass.BindVertexBuffer(1, *command.mesh->GetSkinBuffer());
            }
            pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
            pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
            boundMesh = command.mesh;
        }
        const Buffer* instanceBuffer = command.instanceBuffer ? command.instanceBuffer : m_instances.get();
        if (instanceBuffer != boundInstances) {
            pass.BindVertexStorageBuffer(0, *instanceBuffer);
            boundInstances = instanceBuffer;
        }
        if (command.instanceBuffer) {
            pass.PushVertexUniforms(1, BatchUniforms{{command.instanceFirst, 1, 0, 0}});
            pass.DrawIndexed(command.indexCount, command.instanceCount, command.firstIndex);
        } else {
            pass.PushVertexUniforms(1, BatchUniforms{{static_cast<std::uint32_t>(first), 0, 0, 0}});
            pass.DrawIndexed(command.indexCount, instanceCount, command.firstIndex);
        }

        ++m_stats.drawCalls;
        m_stats.triangleCount +=
            command.indexCount / 3 * (command.instanceBuffer ? command.instanceCount : instanceCount);
        first = end;
    }
}

} // namespace Spot
