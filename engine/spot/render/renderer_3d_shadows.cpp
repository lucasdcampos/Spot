#include "spot/render/renderer_3d_shadows.h"

#include "spot/core/debug/profiler.h"

#include "shadow.frag.h"
#include "shadow.vert.h"
#include "shadow_cutout.frag.h"
#include "skinned_shadow.vert.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <span>
#include <tuple>

namespace Spot::Detail {

namespace {

// Takes world positions to the UVs of a shadow tile, Y down, keeping the depth.
const Matrix4 TileFromClip = Matrix4::Translation({0.5f, 0.5f, 0.0f}) * Matrix4::Scale({0.5f, -0.5f, 1.0f});

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

ShadowRenderer::ShadowRenderer(Renderer3DState& state) : m_state(state) {
    GraphicsDevice& device = *state.device;
    const auto shadowVertexShader = device.CreateShader(Shaders::ShadowVert);
    const auto shadowFragmentShader = device.CreateShader(Shaders::ShadowFrag);
    const auto cutoutShadowFragmentShader = device.CreateShader(Shaders::ShadowCutoutFrag);
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
        m_pipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Front;
        m_mirroredPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::None;
        m_doubleSidedPipeline = device.CreatePipeline(desc);
        desc.fragmentShader = cutoutShadowFragmentShader.get();
        m_doubleSidedCutoutPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Back;
        m_cutoutPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Front;
        m_mirroredCutoutPipeline = device.CreatePipeline(desc);
    }

    const auto skinnedShadow = device.CreateShader(Shaders::SkinnedShadowVert);
    if (skinnedShadow && shadowFragmentShader && cutoutShadowFragmentShader) {
        for (std::size_t i = 0; i < m_skinnedPipelines.size(); ++i) {
            m_skinnedPipelines[i] = device.CreatePipeline(
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
    m_sampler = device.CreateSampler({.compare = CompareOp::LessOrEqual});
    m_noShadows =
        device.CreateTexture({.width = 1, .height = 1, .format = TextureFormat::Depth32Float, .renderTarget = true});
}

bool ShadowRenderer::IsValid() const {
    return std::ranges::all_of(m_skinnedPipelines, [](const auto& p) { return !!p; }) && m_pipeline &&
           m_doubleSidedPipeline && m_cutoutPipeline && m_doubleSidedCutoutPipeline && m_mirroredPipeline &&
           m_mirroredCutoutPipeline && m_sampler && m_noShadows;
}

void ShadowRenderer::Bind(RenderPass& pass) const {
    pass.BindFragmentTexture(1, m_cascadeShadows ? *m_cascadeShadows : *m_noShadows, *m_sampler);
    pass.BindFragmentTexture(2, m_localShadows ? *m_localShadows : *m_noShadows, *m_sampler);
    pass.BindFragmentTexture(9, m_skyOcclusionDrawn ? *m_skyOcclusion : *m_noShadows, *m_sampler);
}

void ShadowRenderer::Draw(SelectedLights& lights, ShadowUniforms& shadows, bool skyOcclusion) {
    SPOT_PROFILE_SCOPE("Renderer3D::DrawShadows");
    // Shadow views go through the casters in one order, kept for the frame.
    m_order.clear();
    for (std::size_t i = 0; i < m_state.commands.size(); ++i) {
        // Transparent surfaces let light through, so they cast no shadow.
        if (m_state.commands[i].castsShadows && m_state.commands[i].alphaMode != AlphaMode::Transparent) {
            m_order.push_back(static_cast<std::uint32_t>(i));
        }
    }
    const auto shadowKey = [this](std::uint32_t index) {
        const DrawCommand& c = m_state.commands[index];
        const bool cutout = c.alphaMode == AlphaMode::Cutout;
        return std::make_tuple(c.doubleSided, c.mirrored, cutout, cutout ? c.look : 0u, c.mesh, c.firstIndex);
    };
    std::ranges::stable_sort(m_order, [&](std::uint32_t a, std::uint32_t b) { return shadowKey(a) < shadowKey(b); });
    // Views record their draws before their casters reach the GPU, which takes one upload at the end. The buffer has
    // room for every view to draw every caster, so it never has to grow while draws refer to it, and starts in fresh
    // memory, so views drawn by an earlier End() in the frame keep their own.
    m_frameCasters.clear();
    std::size_t shadowInstances = 0;
    for (const std::uint32_t index : m_order) {
        shadowInstances += m_state.commands[index].instanceCount;
    }
    // The sky's occlusion is one more view.
    const std::size_t maxCasters = (MaxShadowCascades + Renderer3D::MaxShadowTiles + 1) * shadowInstances + 1;
    if (!m_casters || m_casters->GetSize() < maxCasters * sizeof(std::uint32_t)) {
        m_casters = m_state.device->CreateBuffer(
            {.usage = BufferUsage::Storage, .size = static_cast<std::uint32_t>(maxCasters * sizeof(std::uint32_t))});
    }
    if (!m_casters) {
        return;
    }
    m_state.device->UploadToBuffer(*m_casters, std::array<std::uint32_t, 1>{}, 0, UploadMode::Discard);
    DrawViews(lights, shadows, skyOcclusion);
    // Smaller than the buffer, so it fills the memory the draws were recorded with instead of moving to new memory.
    if (!m_frameCasters.empty()) {
        m_state.device->UploadToBuffer(*m_casters, m_frameCasters, 0, UploadMode::Preserve);
    }
}

void ShadowRenderer::DrawViews(SelectedLights& lights, ShadowUniforms& shadows, bool skyOcclusion) {
    if (!m_noShadowsCleared) {
        m_state.context->BeginRenderPass({.depthTarget = m_noShadows.get(), .clearDepth = 1.0f, .depthOnly = true});
        m_noShadowsCleared = true;
    }
    m_skyOcclusionDrawn = false;
    if (m_state.commands.empty() && m_state.surfaces.empty()) {
        return;
    }
    if (skyOcclusion && m_state.config.skyOcclusionSize > 0) {
        m_skyOcclusionDrawn = DrawSkyOcclusion(shadows);
    }
    if (m_state.config.shadowMapSize > 0) {
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
    if (m_state.config.localShadowMapSize > 0) {
        DrawLocalShadows(lights, shadows);
    }
}

bool ShadowRenderer::DrawCascades(const LightData& light, ShadowUniforms& shadows) {
    const std::uint32_t count = std::clamp(m_state.config.shadowCascadeCount, 1u, MaxShadowCascades);
    const std::uint32_t size = m_state.config.shadowMapSize;
    const std::uint32_t columns = count > 1 ? 2 : 1;
    const std::uint32_t rows = count > 2 ? 2 : 1;
    if (!m_cascadeShadows || m_cascadeShadows->GetWidth() != columns * size ||
        m_cascadeShadows->GetHeight() != rows * size) {
        m_cascadeShadows = m_state.device->CreateTexture({.width = columns * size,
                                                          .height = rows * size,
                                                          .format = TextureFormat::Depth32Float,
                                                          .renderTarget = true});
        if (!m_cascadeShadows) {
            return false;
        }
    }

    const float nearPlane = m_state.camera.nearPlane;
    const auto ends =
        GetCascadeEnds(nearPlane, std::min(m_state.config.shadowDistance, m_state.camera.farPlane), count);
    const Vector3 forward = m_state.camera.GetForward();
    shadows.cascadeEnds = {ends[0], ends[1], ends[2], ends[3]};
    shadows.cameraForward = {forward.x, forward.y, forward.z, static_cast<float>(count)};
    shadows.cascadeLayout = {static_cast<float>(columns), static_cast<float>(rows),
                             1.0f / static_cast<float>(columns * size), 1.0f / static_cast<float>(rows * size)};

    std::array<ShadowView, MaxShadowCascades> views{};
    std::array<std::uint32_t, MaxShadowCascades> surfaceViews{};
    for (std::uint32_t cascade = 0; cascade < count; ++cascade) {
        const float start = cascade == 0 ? nearPlane : ends[cascade - 1];
        views[cascade] =
            GetCascadeView(m_state.camera, m_state.aspectRatio, start, ends[cascade], light.direction, size);
        surfaceViews[cascade] =
            m_state.PrepareSurfaces(*m_state.context, SurfacePass::Shadow, views[cascade].viewProjection, true);
    }
    auto pass = m_state.context->BeginRenderPass(
        {.depthTarget = m_cascadeShadows.get(), .clearDepth = 1.0f, .depthOnly = true});
    std::array<float, MaxShadowCascades> texelSizes{};
    for (std::uint32_t cascade = 0; cascade < count; ++cascade) {
        const ShadowView& view = views[cascade];
        shadows.cascadeViews[cascade] = TileFromClip * view.viewProjection;
        texelSizes[cascade] = view.texelSize;
        pass.SetViewport({.x = static_cast<float>(cascade % columns * size),
                          .y = static_cast<float>(cascade / columns * size),
                          .width = static_cast<float>(size),
                          .height = static_cast<float>(size)});
        DrawCasters(pass, view.viewProjection, true);
        m_state.DrawSurfaces(pass, SurfacePass::Shadow, view.viewProjection, true, surfaceViews[cascade]);
    }
    shadows.cascadeTexelSizes = {texelSizes[0], texelSizes[1], texelSizes[2], texelSizes[3]};
    return true;
}

void ShadowRenderer::DrawLocalShadows(SelectedLights& lights, ShadowUniforms& shadows) {
    // The lights that matter most to the view get tiles first; a spot light may still fit where a point light does not.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < lights.lights.size(); ++i) {
        const LightData& light = *lights.lights[i];
        if (light.type != LightType::Directional && light.castsShadows && light.range > 0.0f &&
            IsSphereInView(m_state.viewProjection, light.position, light.range)) {
            order.push_back(i);
        }
    }
    std::ranges::stable_sort(order, {},
                             [&](std::size_t i) { return GetLightRank(*lights.lights[i], m_state.camera.position); });
    std::size_t tileCount = 0;
    for (const std::size_t i : order) {
        const std::size_t tiles = lights.lights[i]->type == LightType::Point ? 6 : 1;
        if (tileCount + tiles <= Renderer3D::MaxShadowTiles) {
            lights.shadows[i] = static_cast<float>(tileCount);
            tileCount += tiles;
        }
    }
    if (tileCount == 0) {
        return;
    }

    constexpr std::uint32_t Columns = 4;
    constexpr std::uint32_t Rows = Renderer3D::MaxShadowTiles / Columns;
    const std::uint32_t size = m_state.config.localShadowMapSize;
    if (!m_localShadows || m_localShadows->GetWidth() != Columns * size) {
        m_localShadows = m_state.device->CreateTexture({.width = Columns * size,
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

    std::array<ShadowView, Renderer3D::MaxShadowTiles> views{};
    std::array<std::uint32_t, Renderer3D::MaxShadowTiles> surfaceViews{};
    for (std::size_t i = 0; i < lights.lights.size(); ++i) {
        if (lights.shadows[i] < 0.0f || lights.lights[i]->type == LightType::Directional) {
            continue;
        }
        const LightData& light = *lights.lights[i];
        const auto first = static_cast<std::uint32_t>(lights.shadows[i]);
        const std::uint32_t faces = light.type == LightType::Point ? 6 : 1;
        for (std::uint32_t face = 0; face < faces; ++face) {
            const std::uint32_t tile = first + face;
            views[tile] =
                light.type == LightType::Point ? GetPointShadowView(light, face, size) : GetSpotShadowView(light, size);
            surfaceViews[tile] =
                m_state.PrepareSurfaces(*m_state.context, SurfacePass::Shadow, views[tile].viewProjection, false);
        }
    }
    auto pass =
        m_state.context->BeginRenderPass({.depthTarget = m_localShadows.get(), .clearDepth = 1.0f, .depthOnly = true});
    for (std::size_t i = 0; i < lights.lights.size(); ++i) {
        if (lights.shadows[i] < 0.0f || lights.lights[i]->type == LightType::Directional) {
            continue;
        }
        const LightData& light = *lights.lights[i];
        const auto first = static_cast<std::uint32_t>(lights.shadows[i]);
        const std::uint32_t faces = light.type == LightType::Point ? 6 : 1;
        for (std::uint32_t face = 0; face < faces; ++face) {
            const std::uint32_t tile = first + face;
            const ShadowView& view = views[tile];
            shadows.tileViews[tile] = TileFromClip * view.viewProjection;
            lights.shadowTexelSizes[i] = view.texelSize;
            pass.SetViewport({.x = static_cast<float>(tile % Columns * size),
                              .y = static_cast<float>(tile / Columns * size),
                              .width = static_cast<float>(size),
                              .height = static_cast<float>(size)});
            DrawCasters(pass, view.viewProjection, false);
            m_state.DrawSurfaces(pass, SurfacePass::Shadow, view.viewProjection, false, surfaceViews[tile]);
        }
    }
}

bool ShadowRenderer::DrawSkyOcclusion(ShadowUniforms& shadows) {
    const std::uint32_t size = m_state.config.skyOcclusionSize;
    if (!m_skyOcclusion || m_skyOcclusion->GetWidth() != size) {
        m_skyOcclusion = m_state.device->CreateTexture(
            {.width = size, .height = size, .format = TextureFormat::Depth32Float, .renderTarget = true});
        if (!m_skyOcclusion) {
            return false;
        }
    }
    const float distance = std::max(m_state.config.skyOcclusionDistance, 1.0f);
    const ShadowView view = GetSkyOcclusionView(m_state.camera.position, distance, size);
    // What is above the view still covers what is under it, as its depth is clamped to the top.
    const std::uint32_t surfaceView =
        m_state.PrepareSurfaces(*m_state.context, SurfacePass::Shadow, view.viewProjection, true);
    auto pass =
        m_state.context->BeginRenderPass({.depthTarget = m_skyOcclusion.get(), .clearDepth = 1.0f, .depthOnly = true});
    DrawCasters(pass, view.viewProjection, true);
    m_state.DrawSurfaces(pass, SurfacePass::Shadow, view.viewProjection, true, surfaceView);
    shadows.skyOcclusionView = TileFromClip * view.viewProjection;
    // Cover is looked for over a couple of meters, enough to see past the edge of a roof, and a few texels at least.
    shadows.skyOcclusionParams = {1.0f, std::max(2.0f, 4.0f * view.texelSize), 1.0f / (2.0f * distance),
                                  view.texelSize};
    return true;
}

void ShadowRenderer::BindCutout(RenderPass& pass, const DrawCommand& command, const Texture*& boundTexture,
                                const Sampler*& boundSampler) const {
    // Cutout shadows follow the mesh's UVs, even for textures projected from world space.
    const Texture* texture = command.textures[0] ? command.textures[0] : m_state.whiteTexture.get();
    const Sampler* sampler = command.nearestFiltering ? m_state.nearestSampler.get() : m_state.sampler.get();
    if (texture != boundTexture || sampler != boundSampler) {
        pass.BindFragmentTexture(0, *texture, *sampler);
        boundTexture = texture;
        boundSampler = sampler;
    }
    pass.PushFragmentUniforms(0, Vector4{command.color.a, command.alphaCutoff, 0.0f, 0.0f});
}

void ShadowRenderer::DrawCasters(RenderPass& pass, const Matrix4& viewProjection, bool ignoreNear) {
    SPOT_PROFILE_SCOPE("Renderer3D::DrawShadowCasters");
    if (m_state.commands.empty()) {
        return;
    }
    // The view's casters, gathered so those that cast the same shadow are side by side, whatever the view leaves out
    // between them, and read through their indices. Transparent surfaces let light through, so they cast no shadow.
    // The view's casters go after those of the views drawn before, in one list that DrawShadows() uploads once.
    const Frustum frustum(viewProjection, ignoreNear);
    const std::size_t viewStart = m_frameCasters.size();
    bool worldCasters = false;
    for (const std::uint32_t index : m_order) {
        const DrawCommand& command = m_state.commands[index];
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
    pass.PushVertexUniforms(0, MakeFrameUniforms(viewProjection, m_state.camera, ignoreNear));
    pass.BindVertexStorageBuffer(0, *m_state.instances);
    pass.BindVertexStorageBuffer(1, *m_casters);
    const Pipeline* boundPipeline = nullptr;
    const Texture* boundTexture = nullptr;
    const Sampler* boundSampler = nullptr;
    for (std::size_t first = 0; first < casters.size();) {
        const DrawCommand& command = m_state.commands[casters[first]];
        std::size_t end = first + 1;
        while (end < casters.size() && HaveSameShadow(command, m_state.commands[casters[end]])) {
            ++end;
        }
        const Pipeline* pipeline = GetPipeline(command);
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
        }
        if (command.alphaMode == AlphaMode::Cutout) {
            BindCutout(pass, command, boundTexture, boundSampler);
        }
        if (command.skinCount) {
            pass.BindVertexStorageBuffer(2, *m_state.palettes);
            pass.BindVertexBuffer(1, *command.mesh->GetSkinBuffer());
        }
        pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
        pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
        pass.PushVertexUniforms(1, BatchUniforms{{static_cast<std::uint32_t>(viewStart + first), 0, 0, 0}});
        pass.DrawIndexed(command.indexCount, static_cast<std::uint32_t>(end - first), command.firstIndex);
        ++m_state.stats.shadowDrawCalls;
        first = end;
    }
    if (!worldCasters) {
        return;
    }
    // Batches of worlds the view reaches are culled instance by instance into the casters, as those cast into small
    // views, such as a point light's, are mostly out of them.
    for (const std::uint32_t index : m_order) {
        const DrawCommand& command = m_state.commands[index];
        if (!command.instanceBuffer || !frustum.Contains(command.bounds.center, command.bounds.radius)) {
            continue;
        }
        const auto batchStart = static_cast<std::uint32_t>(m_frameCasters.size());
        for (std::uint32_t i = command.instanceFirst; i < command.instanceFirst + command.instanceCount; ++i) {
            const InstanceData& instance = command.instanceData[i];
            const Vector4& bounds = instance.bounds;
            if (!frustum.Contains({bounds.x, bounds.y, bounds.z}, bounds.w)) {
                continue;
            }
            // The level of detail the camera sees casts the shadow.
            const float minSize = std::bit_cast<float>(instance.skin[2]);
            const float maxSize = std::bit_cast<float>(instance.skin[3]);
            if (minSize > 0.0f || maxSize < std::numeric_limits<float>::infinity()) {
                const float size = GetScreenSize(m_state.camera, {{bounds.x, bounds.y, bounds.z}, bounds.w});
                if (size < minSize || size >= maxSize) {
                    continue;
                }
            }
            m_frameCasters.push_back(i);
        }
        const auto visible = static_cast<std::uint32_t>(m_frameCasters.size()) - batchStart;
        if (visible == 0) {
            continue;
        }
        const Pipeline* pipeline = GetPipeline(command);
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
        }
        if (command.alphaMode == AlphaMode::Cutout) {
            BindCutout(pass, command, boundTexture, boundSampler);
        }
        pass.BindVertexStorageBuffer(0, *command.instanceBuffer);
        pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
        pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
        pass.PushVertexUniforms(1, BatchUniforms{{batchStart, 0, 0, 0}});
        pass.DrawIndexed(command.indexCount, visible, command.firstIndex);
        ++m_state.stats.shadowDrawCalls;
    }
}

const Pipeline* ShadowRenderer::GetPipeline(const DrawCommand& command) const {
    if (command.skinCount) {
        const auto variant = (command.alphaMode == AlphaMode::Cutout ? 3u : 0u) + (command.doubleSided ? 2u
                                                                                   : command.mirrored  ? 1u
                                                                                                       : 0u);
        return m_skinnedPipelines[variant].get();
    }
    if (command.alphaMode == AlphaMode::Cutout) {
        return command.doubleSided ? m_doubleSidedCutoutPipeline.get()
               : command.mirrored  ? m_mirroredCutoutPipeline.get()
                                   : m_cutoutPipeline.get();
    }
    return command.doubleSided ? m_doubleSidedPipeline.get()
           : command.mirrored  ? m_mirroredPipeline.get()
                               : m_pipeline.get();
}

} // namespace Spot::Detail
