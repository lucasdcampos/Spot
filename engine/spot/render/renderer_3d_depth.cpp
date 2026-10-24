#include "shadow.frag.h"
#include "shadow.vert.h"
#include "shadow_cutout.frag.h"
#include "skinned_shadow.vert.h"
#include "spot/core/debug/assert.h"
#include "spot/core/debug/profiler.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/render_context.h"
#include "spot/render/renderer_3d.h"
#include "spot/render/shadow_views.h"
#include <array>
#include <cmath>
#include <cstddef>

namespace Spot {
const Texture* Renderer3D::RenderCameraDepth(RenderContext& context) {
    SPOT_PROFILE_SCOPE("Renderer3D::RenderCameraDepth");
    SPOT_ASSERT(!m_context, "Camera depth is drawn after Renderer3D::End()");
    if (!IsValid())
        return nullptr;
    const auto width = static_cast<std::uint32_t>(m_viewSize.x), height = static_cast<std::uint32_t>(m_viewSize.y);
    if (!m_cameraDepth || m_cameraDepth->GetWidth() != width || m_cameraDepth->GetHeight() != height) {
        m_cameraDepth = m_device->CreateTexture(
            {.width = width, .height = height, .format = TextureFormat::Depth32Float, .renderTarget = true});
    }
    if (!m_cameraDepth)
        return nullptr;
    if (!m_cameraDepthPipelines[0]) {
        const auto vertex = m_device->CreateShader(Detail::Shaders::ShadowVert);
        const auto skinned = m_device->CreateShader(Detail::Shaders::SkinnedShadowVert);
        const auto opaque = m_device->CreateShader(Detail::Shaders::ShadowFrag);
        const auto cutout = m_device->CreateShader(Detail::Shaders::ShadowCutoutFrag);
        if (!vertex || !skinned || !opaque || !cutout)
            return nullptr;
        for (std::size_t i = 0; i < m_cameraDepthPipelines.size(); ++i) {
            std::vector<VertexBufferLayout> layouts{
                {.stride = sizeof(MeshVertex),
                 .attributes = {{0, VertexFormat::Float3, offsetof(MeshVertex, position)},
                                {1, VertexFormat::Float2, offsetof(MeshVertex, uv)}}}};
            if (i >= 6) {
                layouts.push_back(Mesh::GetSkinLayout(2));
            }
            m_cameraDepthPipelines[i] =
                m_device->CreatePipeline({.vertexShader = i >= 6 ? skinned.get() : vertex.get(),
                                          .fragmentShader = i % 6 >= 3 ? cutout.get() : opaque.get(),
                                          .vertexBuffers = layouts,
                                          .cullMode = i % 3 == 0   ? CullMode::Back
                                                      : i % 3 == 1 ? CullMode::Front
                                                                   : CullMode::None,
                                          .depthOnly = true,
                                          .depth = DepthState{}});
        }
    }
    for (const auto& pipeline : m_cameraDepthPipelines)
        if (!pipeline)
            return nullptr;
    m_cameraDepthOrder.clear();
    for (std::size_t i = 0; i < m_commands.size(); ++i) {
        if (m_commands[i].visible && m_commands[i].alphaMode != AlphaMode::Transparent)
            m_cameraDepthOrder.push_back(static_cast<std::uint32_t>(i));
    }
    if (!m_cameraDepthOrder.empty()) {
        const auto bytes = static_cast<std::uint32_t>(m_cameraDepthOrder.size() * sizeof(std::uint32_t));
        if (!m_cameraDepthIndices || m_cameraDepthIndices->GetSize() < bytes)
            m_cameraDepthIndices = m_device->CreateBuffer({.usage = BufferUsage::Storage, .size = bytes});
        if (!m_cameraDepthIndices)
            return nullptr;
        m_device->UploadToBuffer(*m_cameraDepthIndices, m_cameraDepthOrder, 0, UploadMode::Discard);
    }
    const std::uint32_t surfaceView = PrepareSurfaces(context, SurfacePass::Depth, m_viewProjection, false);
    auto pass = context.BeginRenderPass({.depthTarget = m_cameraDepth.get(), .clearDepth = 1.0f, .depthOnly = true});
    if (!m_cameraDepthOrder.empty()) {
        pass.BindVertexStorageBuffer(0, *m_instances);
        pass.BindVertexStorageBuffer(1, *m_cameraDepthIndices);
        // The view, and its planes, which batches of render worlds are culled against in the shader.
        struct {
            Matrix4 viewProjection;
            std::array<Vector4, 6> cullPlanes;
            Vector4 lodView;
        } view{m_viewProjection, Detail::Frustum(m_viewProjection).GetPlanes(), Detail::GetLodView(m_camera)};
        pass.PushVertexUniforms(0, view);
        for (std::size_t i = 0; i < m_cameraDepthOrder.size(); ++i) {
            const auto& command = m_commands[m_cameraDepthOrder[i]];
            const bool cutout = command.alphaMode == AlphaMode::Cutout;
            const auto variant = static_cast<std::size_t>((cutout ? 3 : 0) + (command.doubleSided ? 2
                                                                              : command.mirrored  ? 1
                                                                                                  : 0));
            pass.BindPipeline(*m_cameraDepthPipelines[variant + (command.skinCount ? 6 : 0)]);
            if (command.skinCount) {
                pass.BindVertexStorageBuffer(2, *m_palettes);
                pass.BindVertexBuffer(1, *command.mesh->GetSkinBuffer());
            }
            if (cutout) {
                pass.BindFragmentTexture(0, command.textures[0] ? *command.textures[0] : *m_whiteTexture,
                                         command.nearestFiltering ? *m_nearestSampler : *m_sampler);
                pass.PushFragmentUniforms(0, Vector4{command.color.a, command.alphaCutoff, 0, 0});
            }
            pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
            pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
            if (command.instanceBuffer) {
                pass.BindVertexStorageBuffer(0, *command.instanceBuffer);
                pass.PushVertexUniforms(1, std::array<std::uint32_t, 4>{command.instanceFirst, 1, 0, 0});
                pass.DrawIndexed(command.indexCount, command.instanceCount, command.firstIndex);
                pass.BindVertexStorageBuffer(0, *m_instances);
            } else {
                pass.PushVertexUniforms(1, std::array<std::uint32_t, 4>{static_cast<std::uint32_t>(i), 0, 0, 0});
                pass.DrawIndexed(command.indexCount, 1, command.firstIndex);
            }
        }
    }
    DrawSurfaces(pass, SurfacePass::Depth, m_viewProjection, false, surfaceView);
    return m_cameraDepth.get();
}
} // namespace Spot
