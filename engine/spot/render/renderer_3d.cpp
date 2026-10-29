#include "spot/render/renderer_3d.h"

#include "spot/core/debug/assert.h"
#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"
#include "spot/render/light_clusters.h"
#include "spot/render/material.h"
#include "spot/render/mesh.h"
#include "spot/render/model.h"
#include "spot/render/render_world.h"
#include "spot/render/renderer_3d_internal.h"
#include "spot/render/renderer_3d_meshes.h"
#include "spot/render/renderer_3d_probes.h"
#include "spot/render/renderer_3d_shadows.h"
#include "spot/render/renderer_3d_sky.h"
#include "spot/render/renderer_3d_worlds.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace Spot {

namespace Detail {

Matrix4 GetNormalMatrix(const Matrix4& m, TextureSpace textureSpace) {
    // Its cofactors, signed by its determinant, since the shader normalizes the normals anyway.
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
                      Vector4{textureSpace == TextureSpace::Object ? 1.0f : 0.0f, 0.0f, 0.0f, sign}};
    return normal;
}

FogUniforms GetFogUniforms(const std::optional<FogData>& fog, Vector3 cameraPosition) {
    if (!fog) {
        return {.color = {0.0f, 0.0f, 0.0f, 0.0f}, .params = {}, .camera = {}};
    }
    return {.color = {fog->color.r, fog->color.g, fog->color.b, 1.0f},
            .params = {std::max(fog->density, 0.0f), std::max(fog->start, 0.0f), std::max(fog->heightFalloff, 0.0f),
                       fog->baseHeight},
            .camera = {cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f}};
}

float GetLightRank(const LightData& light, Vector3 cameraPosition) {
    return light.type == LightType::Directional
               ? -1.0f
               : std::max(Distance(light.position, cameraPosition) - light.range, 0.0f);
}

Renderer3DState::Renderer3DState(GraphicsDevice& graphicsDevice, const Renderer3DConfig& rendererConfig)
    : device(&graphicsDevice), config(rendererConfig) {
    sampler = device->CreateSampler(
        {.filter = config.filter, .addressMode = TextureAddressMode::Repeat, .maxAnisotropy = config.maxAnisotropy});
    nearestSampler =
        device->CreateSampler({.filter = TextureFilter::Nearest, .addressMode = TextureAddressMode::Repeat});
    clampSampler = device->CreateSampler();
    whiteTexture = device->CreateTexture({.width = 1, .height = 1});
    if (whiteTexture) {
        device->UploadToTexture(*whiteTexture, std::array<std::uint8_t, 4>{255, 255, 255, 255});
    }
}

std::uint32_t Renderer3DState::PrepareSurfaces(RenderContext& renderContext, SurfacePass kind, const Matrix4& view,
                                               bool ignoreNear) {
    const std::uint32_t viewIndex = surfaceViewIndex++;
    const SurfacePrepareContext prepare{
        *device, renderContext, kind, camera, view, Frustum(view, ignoreNear).GetPlanes(), viewIndex};
    for (CustomSurface* surface : surfaces) {
        surface->Prepare(prepare);
    }
    return viewIndex;
}

void Renderer3DState::DrawSurfaces(RenderPass& pass, SurfacePass kind, const Matrix4& view, bool ignoreNear,
                                   std::uint32_t viewIndex) {
    if (surfaces.empty()) {
        return;
    }
    SPOT_PROFILE_SCOPE("Renderer3D::DrawSurfaces");
    const bool lit = kind == SurfacePass::Lit;
    const SurfaceDrawContext drawContext{
        .device = *device,
        .pass = pass,
        .kind = kind,
        .camera = camera,
        .viewProjection = view,
        .planes = Frustum(view, ignoreNear).GetPlanes(),
        .colorFormat = lit ? config.targetFormat : std::nullopt,
        .sampleCount = lit ? config.sampleCount : 1,
        .depth = lit                           ? DepthState{.format = config.depthFormat}
                 : kind == SurfacePass::Shadow ? Renderer3D::GetShadowDepthState()
                                               : DepthState{},
        .viewIndex = viewIndex,
    };
    for (CustomSurface* surface : surfaces) {
        switch (kind) {
        case SurfacePass::Lit:
            surface->DrawLit(drawContext);
            break;
        case SurfacePass::Shadow:
            surface->DrawShadow(drawContext);
            break;
        case SurfacePass::Depth:
            surface->DrawDepth(drawContext);
            break;
        }
    }
}

} // namespace Detail

namespace {

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
    Vector4 probeAtlas;
    // What lights a point in place of the sky it is covered from.
    Color skyBounce;
};

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
                                 [&](const LightData* light) { return Detail::GetLightRank(*light, cameraPosition); });
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

// Draws the frame: lights it, culls and sorts its meshes through the mesh batcher, has the shadow renderer and the
// sky renderer draw their parts, the probe renderer capture the reflection probes, and the world culler cull the
// worlds' instances, then opens the view's pass.
struct Renderer3D::Impl {
    Impl(GraphicsDevice& device, const Renderer3DConfig& config)
        : state(device, config), meshes(state), shadows(state), sky(state), worlds(state), probes(state) {
        if (config.ambientOcclusion.enabled) {
            ambientOcclusion = AmbientOcclusion::Create(device);
        }
    }

    bool IsValid() const {
        return state.IsValid() && meshes.IsValid() && shadows.IsValid() && sky.IsValid() && probes.IsValid() &&
               (!state.config.ambientOcclusion.enabled || ambientOcclusion);
    }

    void End();
    // Makes the camera the one the frame is drawn through, into the target, of the size.
    void SetView(const Camera3D& camera, const RenderPassDesc& target, Vector2 viewSize);
    // Draws the frame's sky, surfaces and meshes into the target as the camera sees them, after the shadows of that
    // view. The main view also culls the worlds on the GPU, is darkened by ambient occlusion and lit by the reflection
    // probes; the faces of a probe's capture are not, so they hold the light that reaches the probe from outside.
    void DrawView(Detail::SelectedLights selected, bool main);
    // Draws the six faces of the probe and blurs them into the light it gives, then goes back to the main view.
    void CaptureProbe(const ReflectionProbeData& probe, const Detail::SelectedLights& selected);
    // Sends the lights and the view's clusters of them to their buffers.
    bool UploadLights(const Detail::SelectedLights& lights);
    void BindLighting(RenderPass& pass, const Detail::SelectedLights& lights, const Detail::ShadowUniforms& shadows,
                      bool main, const Texture* occlusion);

    Detail::Renderer3DState state;
    Detail::MeshBatcher meshes;
    Detail::ShadowRenderer shadows;
    Detail::SkyRenderer sky;
    Detail::WorldCuller worlds;
    Detail::ProbeRenderer probes;
    std::unique_ptr<AmbientOcclusion> ambientOcclusion;

    std::vector<LightData> lights;
    std::vector<ReflectionProbeData> probeData;
    std::optional<SkyData> skyData;
    std::optional<Detail::SkyLighting> skyLighting;
    // Of the camera's rotation and projection only, which turns clip-space positions into directions.
    Matrix4 inverseSkyViewProjection;
    bool warnedAboutLights = false;
    // Whether End() drew the camera's depth, for ambient occlusion, which RenderCameraDepth() then gives.
    bool cameraDepthDrawn = false;
    // The frame's lights, the view's clusters and their lists of lights.
    std::unique_ptr<Buffer> lightBuffer;
    std::unique_ptr<Buffer> clusterBuffer;
    std::unique_ptr<Buffer> lightIndexBuffer;
    std::uint32_t directionalLightCount = 0;
    float clusterNear = 0.1f;
    float clusterScale = 1.0f;
};

void Renderer3D::Impl::End() {
    skyLighting.reset();
    sky.SetLit(false);
    if (skyData) {
        skyLighting = Detail::GetSkyLighting(*skyData);
        if (skyData->lightsScene) {
            // The sun casts shadows while it is up, and the moon once it has set: the first directional light that
            // casts them takes the cascades.
            for (const std::optional<LightData>& light : {skyLighting->sun, skyLighting->moon}) {
                if (light && (light->color.r > 0.0f || light->color.g > 0.0f || light->color.b > 0.0f)) {
                    LightData lit = *light;
                    lit.castsShadows = skyData->sunCastsShadows;
                    lights.push_back(lit);
                }
            }
            // The sky lights from every direction, through its captured light; should that fail, it lights evenly.
            sky.SetLit(sky.Update(*skyData, *skyLighting));
            if (!sky.IsLit()) {
                lights.push_back({.type = LightType::Ambient, .color = skyLighting->ambient});
            }
        }
    }
    const Detail::SelectedLights selected = SelectLights(lights, state.camera.position, warnedAboutLights);

    probes.Select(probeData);
    if (const std::optional<ReflectionProbeData> capture = probes.GetPendingCapture()) {
        CaptureProbe(*capture, selected);
    }
    probes.Upload();
    DrawView(selected, true);
}

void Renderer3D::Impl::SetView(const Camera3D& camera, const RenderPassDesc& target, Vector2 viewSize) {
    state.target = target;
    state.viewProjection = camera.GetViewProjection(viewSize);
    inverseSkyViewProjection = Inverse(camera.GetProjection(viewSize) * Matrix4::Rotation(Inverse(camera.rotation)));
    state.camera = camera;
    state.viewSize = viewSize;
    state.aspectRatio = viewSize.y > 0.0f ? viewSize.x / viewSize.y : 1.0f;
}

void Renderer3D::Impl::DrawView(Detail::SelectedLights selected, bool main) {
    meshes.Prepare();
    if (!meshes.UploadInstances()) {
        state.commands.clear();
    }
    Detail::ShadowUniforms shadowUniforms{};
    state.context->BeginGpuTimer("Shadows");
    shadows.Draw(selected, shadowUniforms, sky.IsLit());
    state.context->EndGpuTimer();

    if (!UploadLights(selected)) {
        state.commands.clear();
    }
    if (main) {
        state.context->BeginGpuTimer("Visibility");
        worlds.Cull();
        state.context->EndGpuTimer();
    }
    const Texture* occlusion = nullptr;
    if (main && ambientOcclusion && state.config.ambientOcclusion.intensity > 0.0f) {
        state.context->BeginGpuTimer("Ambient occlusion");
        if (const Texture* depth = meshes.RenderCameraDepth(*state.context)) {
            cameraDepthDrawn = true;
            occlusion = ambientOcclusion->Render(*state.context, *depth, state.camera, state.config.ambientOcclusion);
        }
        state.context->EndGpuTimer();
    }
    // The faces of a probe's capture are small and drawn once, so only the main view takes the prepass.
    bool depthPrepassed = false;
    if (main && state.config.depthPrepass) {
        state.context->BeginGpuTimer("Depth prepass");
        depthPrepassed = meshes.DrawDepthPrepass(*state.context);
        state.context->EndGpuTimer();
    }
    state.context->BeginGpuTimer("Opaque");
    const std::uint32_t surfaceView =
        state.PrepareSurfaces(*state.context, SurfacePass::Lit, state.viewProjection, false);
    {
        RenderPassDesc target = state.target;
        // The prepass cleared the depth already, and holds the meshes' now.
        if (depthPrepassed) {
            target.clearDepth.reset();
        }
        auto pass = state.context->BeginRenderPass(target);
        if (skyData) {
            sky.Draw(pass, *skyData, *skyLighting, inverseSkyViewProjection);
        }
        // Surfaces draw before the meshes, as they are mostly large and in front, such as terrain, and so hide much
        // of what comes after them; transparent meshes, last, blend over them.
        BindLighting(pass, selected, shadowUniforms, main, occlusion);
        state.DrawSurfaces(pass, SurfacePass::Lit, state.viewProjection, false, surfaceView);
        meshes.Flush(pass, depthPrepassed);
    }
    if (main) {
        worlds.UpdateOcclusionTiles();
    }
    state.context->EndGpuTimer();
}

void Renderer3D::Impl::CaptureProbe(const ReflectionProbeData& probe, const Detail::SelectedLights& selected) {
    SPOT_PROFILE_SCOPE("Renderer3D::CaptureReflectionProbe");
    const std::optional<RenderPassDesc> faceTarget = probes.GetFaceTarget();
    if (!faceTarget) {
        return;
    }
    const Camera3D camera = state.camera;
    const RenderPassDesc target = state.target;
    const Vector2 viewSize = state.viewSize;
    constexpr auto FaceSize = static_cast<float>(Detail::ProbeCaptureSize);
    state.context->BeginGpuTimer("Reflection probe");
    for (std::uint32_t face = 0; face < 6; ++face) {
        SetView(Detail::ProbeRenderer::GetFaceCamera(probe, face, camera), *faceTarget, {FaceSize, FaceSize});
        DrawView(selected, false);
        probes.FinishFace(face);
    }
    probes.FinishCapture(probe);
    state.context->EndGpuTimer();
    SetView(camera, target, viewSize);
    // The stats are the main view's.
    const std::uint32_t skyLightingUpdates = state.stats.skyLightingUpdates;
    state.stats = {};
    state.stats.skyLightingUpdates = skyLightingUpdates;
    state.stats.reflectionProbeCaptures = 1;
}

bool Renderer3D::Impl::UploadLights(const Detail::SelectedLights& selected) {
    SPOT_PROFILE_SCOPE("Renderer3D::UploadLights");
    const std::vector<LightUniforms> uniforms = GetLightUniforms(selected);
    std::vector<Detail::ClusterLight> clustered;
    directionalLightCount = 0;
    for (std::size_t i = 0; i < selected.lights.size(); ++i) {
        const LightData& light = *selected.lights[i];
        if (light.type == LightType::Directional) {
            ++directionalLightCount;
        } else {
            clustered.push_back({light.position, std::max(light.range, 0.0001f), static_cast<std::uint32_t>(i)});
        }
    }
    const Detail::LightClusters clusters = Detail::BuildLightClusters(state.camera, state.viewSize, clustered);
    clusterNear = clusters.nearDepth;
    clusterScale = clusters.sliceScale;
    state.stats.lights = static_cast<std::uint32_t>(uniforms.size());
    Profiler::Count("Lights", static_cast<double>(uniforms.size()));
    return Detail::UploadStorage(*state.device, lightBuffer, uniforms) &&
           Detail::UploadStorage(*state.device, clusterBuffer, clusters.clusters) &&
           Detail::UploadStorage(*state.device, lightIndexBuffer, clusters.lightIndices);
}

void Renderer3D::Impl::BindLighting(RenderPass& pass, const Detail::SelectedLights& selected,
                                    const Detail::ShadowUniforms& shadowUniforms, bool main, const Texture* occlusion) {
    pass.PushVertexUniforms(0, Detail::MakeFrameUniforms(state.viewProjection, state.camera));
    const Vector3 forward = state.camera.GetForward();
    const LightingUniforms lighting{
        .ambient = selected.ambient,
        .cameraPosition = Detail::ToVector4(state.camera.position, 1.0f),
        .lightCount = {directionalLightCount, main ? probes.GetCount() : 0u, 0, 0},
        .skyLighting = {sky.IsLit() ? 1.0f : 0.0f, static_cast<float>(Detail::SkyRenderer::ReflectionLevels - 1), 0.0f,
                        0.0f},
        .clusterGrid = {Detail::ClusterTilesX, Detail::ClusterTilesY, Detail::ClusterSlices, 0},
        .clusterDepth = {clusterNear, clusterScale, state.viewSize.x, state.viewSize.y},
        .viewForward = Detail::ToVector4(forward),
        .probeAtlas = probes.GetAtlasUniforms(),
        .skyBounce = sky.IsLit() && skyLighting ? skyLighting->bounce : Color::Black,
    };
    pass.PushFragmentUniforms(0, lighting);
    if (lightBuffer && clusterBuffer && lightIndexBuffer) {
        pass.BindFragmentStorageBuffer(0, *lightBuffer);
        pass.BindFragmentStorageBuffer(1, *clusterBuffer);
        pass.BindFragmentStorageBuffer(2, *lightIndexBuffer);
    }
    pass.PushFragmentUniforms(2, shadowUniforms);
    pass.PushFragmentUniforms(3, Detail::GetFogUniforms(state.fog, state.camera.position));
    shadows.Bind(pass);
    sky.Bind(pass);
    pass.BindFragmentTexture(10, occlusion ? *occlusion : *state.whiteTexture, *state.clampSampler);
    probes.Bind(pass, main);
    // The surfaces' own texture slots, white until a surface binds its own.
    for (const std::uint32_t slot : {0u, 3u, 4u, 5u, 6u}) {
        pass.BindFragmentTexture(slot, *state.whiteTexture, *state.sampler);
    }
}

Renderer3D::Renderer3D(GraphicsDevice& device, const Renderer3DConfig& config)
    : m_impl(std::make_unique<Impl>(device, config)) {
    if (!IsValid()) {
        Log::GetEngineLogger().Error("Renderer3D could not create its GPU resources and will not draw");
    }
}

Renderer3D::~Renderer3D() = default;

bool Renderer3D::IsValid() const {
    return m_impl->IsValid();
}

const Camera3D& Renderer3D::GetCamera() const {
    return m_impl->state.camera;
}

Renderer3DStats Renderer3D::GetStats() const {
    return m_impl->state.stats;
}

void Renderer3D::Begin(RenderContext& context, const RenderPassDesc& target, const Camera3D& camera) {
    Detail::Renderer3DState& state = m_impl->state;
    SPOT_ASSERT(!state.context, "Renderer3D::Begin() called twice without End()");
    SPOT_ASSERT(target.depthTarget && !target.depthOnly, "Renderer3D draws in passes with a color and a depth target");
    state.context = &context;
    const Vector2 viewSize =
        target.target
            ? Vector2{static_cast<float>(target.target->GetWidth()), static_cast<float>(target.target->GetHeight())}
            : Vector2{static_cast<float>(context.GetWidth()), static_cast<float>(context.GetHeight())};
    m_impl->SetView(camera, target, viewSize);
    state.commands.clear();
    state.surfaces.clear();
    state.surfaceViewIndex = 0;
    state.paletteData.clear();
    state.fog.reset();
    m_impl->lights.clear();
    m_impl->probeData.clear();
    m_impl->skyData.reset();
    m_impl->cameraDepthDrawn = false;
}

void Renderer3D::End() {
    SPOT_PROFILE_SCOPE("Renderer3D::End");
    Detail::Renderer3DState& state = m_impl->state;
    SPOT_ASSERT(state.context, "Renderer3D::End() called without Begin()");
    state.stats = {};
    if (!IsValid()) {
        state.context->BeginRenderPass(state.target);
        state.context = nullptr;
        return;
    }
    m_impl->End();
    Profiler::Count("Draw calls", state.stats.drawCalls);
    Profiler::Count("Triangles", state.stats.triangleCount);
    Profiler::Count("Shadow draw calls", state.stats.shadowDrawCalls);
    state.context = nullptr;
    ++state.endCount;
    m_impl->worlds.ReleaseUnused();
}

const Texture* Renderer3D::RenderCameraDepth(RenderContext& context) {
    SPOT_PROFILE_SCOPE("Renderer3D::RenderCameraDepth");
    SPOT_ASSERT(!m_impl->state.context, "Camera depth is drawn after Renderer3D::End()");
    if (!IsValid()) {
        return nullptr;
    }
    return m_impl->cameraDepthDrawn ? m_impl->meshes.GetCameraDepth() : m_impl->meshes.RenderCameraDepth(context);
}

void Renderer3D::DrawLight(const LightData& light) {
    SPOT_ASSERT(m_impl->state.context, "Draw calls must come between Begin() and End()");
    m_impl->lights.push_back(light);
}

void Renderer3D::DrawSky(const SkyData& sky) {
    SPOT_ASSERT(m_impl->state.context, "Draw calls must come between Begin() and End()");
    m_impl->skyData = sky;
}

void Renderer3D::DrawFog(const FogData& fog) {
    SPOT_ASSERT(m_impl->state.context, "Draw calls must come between Begin() and End()");
    m_impl->state.fog = fog;
}

void Renderer3D::DrawReflectionProbe(const ReflectionProbeData& probe) {
    SPOT_ASSERT(m_impl->state.context, "Draw calls must come between Begin() and End()");
    m_impl->probeData.push_back(probe);
}

void Renderer3D::DrawMesh(const Mesh& mesh, const Material& material, const Matrix4& transform, bool castsShadows,
                          std::uint32_t submesh) {
    SPOT_ASSERT(m_impl->state.context, "Draw calls must come between Begin() and End()");
    RenderMeshObject object;
    if (Detail::MakeMeshObject(
            {.mesh = &mesh, .transform = transform, .submesh = submesh, .castsShadows = castsShadows}, object)) {
        m_impl->meshes.AddCommand(object, material);
    }
}

void Renderer3D::DrawSkinnedMesh(const Mesh& mesh, const Material& material, std::span<const Matrix4> palette,
                                 const Matrix4& transform, float boundsPadding, bool castsShadows,
                                 std::uint32_t submesh) {
    if (!mesh.GetSkinBuffer() || palette.size() < mesh.GetSkinJointCount()) {
        Log::GetEngineLogger().Warning("Skinned mesh has no valid palette or skin stream");
        return;
    }
    Detail::Renderer3DState& state = m_impl->state;
    const auto before = state.commands.size();
    DrawMesh(mesh, material, transform, castsShadows, submesh);
    if (state.commands.size() == before) {
        return;
    }
    auto& command = state.commands.back();
    command.skinOffset = static_cast<std::uint32_t>(state.paletteData.size());
    command.skinCount = static_cast<std::uint32_t>(palette.size());
    state.paletteData.insert(state.paletteData.end(), palette.begin(), palette.end());
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

void Renderer3D::DrawWorld(const RenderWorld& world) {
    Detail::Renderer3DState& state = m_impl->state;
    Detail::MeshBatcher& meshes = m_impl->meshes;
    SPOT_ASSERT(state.context, "Draw calls must come between Begin() and End()");
    SPOT_PROFILE_SCOPE("Renderer3D::DrawWorld");
    static const Material DefaultMaterial;
    const std::span<const RenderMeshObject> objects = world.GetMeshes();
    const auto sees = [&state](std::uint32_t layer) { return ((state.camera.cullingMask >> layer) & 1u) != 0; };
    // Objects drawn one by one take the level of detail their screen size picks here.
    const auto drawObject = [&](const RenderMeshObject& object, const Material& material, std::uint32_t only) {
        const float size = object.levelCount > 1 || object.levels[0].minScreenSize > 0.0f
                               ? GetScreenSize(state.camera, object.bounds)
                               : 1.0f;
        for (std::uint32_t level = 0; level < object.levelCount; ++level) {
            const RenderMeshLevel& range = object.levels[level];
            if ((only == ~0u || only == level) && size >= range.minScreenSize && size < range.maxScreenSize) {
                meshes.AddCommand(object, material, level);
            }
        }
    };
    Detail::WorldCache* cache = state.config.gpuCulling ? m_impl->worlds.Update(world) : nullptr;
    if (!cache) {
        for (const RenderMeshObject& object : objects) {
            if (object.desc.mesh && sees(object.desc.layer)) {
                drawObject(object, object.desc.material ? *object.desc.material : DefaultMaterial, ~0u);
            }
        }
        return;
    }
    for (const Detail::WorldBatch& batch : cache->batches) {
        if (!sees(batch.layer)) {
            continue;
        }
        const Material& material = batch.material ? *batch.material : DefaultMaterial;
        // Transparent surfaces are drawn one by one, from the farthest, so each blends over what is behind it.
        if (material.alphaMode == AlphaMode::Transparent) {
            for (std::uint32_t i = batch.first; i < batch.first + batch.count; ++i) {
                drawObject(objects[cache->objects[i]], material, cache->levels[i]);
            }
            continue;
        }
        RenderMeshObject proxy{.desc = {.mesh = batch.mesh, .castsShadows = batch.castsShadows},
                               .bounds = batch.bounds,
                               .mirrored = batch.mirrored,
                               .firstIndex = batch.firstIndex,
                               .indexCount = batch.indexCount};
        proxy.levels[0] = {.mesh = batch.mesh, .firstIndex = batch.firstIndex, .indexCount = batch.indexCount};
        proxy.levelCount = 1;
        meshes.AddCommand(proxy, material);
        Detail::DrawCommand& command = state.commands.back();
        command.instanceBuffer = cache->instances.get();
        command.instanceData = cache->instanceData.data();
        command.instanceFirst = batch.first;
        command.instanceCount = batch.count;
        command.batch = static_cast<std::uint32_t>(&batch - cache->batches.data());
    }
}

void Renderer3D::DrawSurface(CustomSurface& surface) {
    SPOT_ASSERT(m_impl->state.context, "Renderer3D::DrawSurface() called outside Begin() and End()");
    m_impl->state.surfaces.push_back(&surface);
}

DepthState Renderer3D::GetShadowDepthState() {
    // Surfaces sloping away from the light cover the most depth per texel, and so need the most bias.
    return {.format = TextureFormat::Depth32Float, .biasSlope = 1.5f, .clamp = true};
}

} // namespace Spot
