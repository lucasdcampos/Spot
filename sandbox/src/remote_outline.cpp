#include "remote_outline.h"
#include "remote_fullscreen.vert.h"
#include "remote_mask.frag.h"
#include "remote_mask.vert.h"
#include "remote_outline.frag.h"

void RemoteOutline::Draw(const Spot::RenderStageContext& stage, Spot::Entity* prop) {
    using namespace Spot;
    if (!prop || stage.viewSize.x <= 0 || stage.viewSize.y <= 0) {
        return;
    }
    if (!m_silhouette) {
        auto vertex = stage.device.CreateShader(Shaders::RemoteMaskVert);
        auto fragment = stage.device.CreateShader(Shaders::RemoteMaskFrag);
        if (!vertex || !fragment) {
            return;
        }
        m_silhouette = stage.device.CreatePipeline({.vertexShader = vertex.get(),
                                                    .fragmentShader = fragment.get(),
                                                    .vertexBuffers = {Mesh::GetVertexLayout()},
                                                    .targetFormat = TextureFormat::Rgba8});
        m_sampler = stage.device.CreateSampler({.filter = TextureFilter::Nearest});
    }
    if (!m_outline || m_format != stage.colorFormat || m_samples != stage.sampleCount) {
        auto vertex = stage.device.CreateShader(Shaders::RemoteFullscreenVert);
        auto fragment = stage.device.CreateShader(Shaders::RemoteOutlineFrag);
        if (!vertex || !fragment) {
            return;
        }
        m_outline = stage.device.CreatePipeline({.vertexShader = vertex.get(),
                                                 .fragmentShader = fragment.get(),
                                                 .blendMode = BlendMode::Alpha,
                                                 .targetFormat = stage.colorFormat,
                                                 .sampleCount = stage.sampleCount});
        m_format = stage.colorFormat;
        m_samples = stage.sampleCount;
    }
    if (!m_mask || m_size != stage.viewSize) {
        m_mask = stage.device.CreateTexture({.width = static_cast<std::uint32_t>(stage.viewSize.x),
                                             .height = static_cast<std::uint32_t>(stage.viewSize.y),
                                             .format = TextureFormat::Rgba8,
                                             .renderTarget = true});
        m_size = stage.viewSize;
    }
    if (!m_mask || !m_silhouette || !m_outline || !m_sampler) {
        return;
    }
    {
        auto pass = stage.context.BeginRenderPass({.target = m_mask.get(), .clearColor = Color{0, 0, 0, 0}});
        pass.BindPipeline(*m_silhouette);
        pass.PushFragmentUniforms(0, Color::White);
        auto draw = [&](const MeshRenderer& renderer) {
            const auto& mesh = renderer.GetMesh();
            if (!renderer.IsActiveAndEnabled() || !mesh) {
                return;
            }
            pass.PushVertexUniforms(0, stage.viewProjection * renderer.GetEntity().GetTransform().GetWorldMatrix());
            pass.BindVertexBuffer(0, mesh->GetVertexBuffer());
            pass.BindIndexBuffer(mesh->GetIndexBuffer(), IndexType::UInt32);
            const auto submesh = renderer.GetSubmesh();
            if (submesh == Mesh::AllSubmeshes) {
                pass.DrawIndexed(mesh->GetIndexCount());
            } else if (submesh < mesh->GetSubmeshes().size()) {
                const auto& part = mesh->GetSubmeshes()[submesh];
                pass.DrawIndexed(part.indexCount, 1, part.firstIndex);
            }
        };
        auto visit = [&](auto&& self, Entity& entity) -> void {
            for (auto* renderer : entity.GetComponents<MeshRenderer>()) {
                draw(*renderer);
            }
            entity.ForEachChild([&](Entity& child) { self(self, child); });
        };
        visit(visit, *prop);
    }
    auto desc = stage.pass;
    desc.depthTarget = nullptr;
    auto pass = stage.context.BeginRenderPass(desc);
    pass.BindPipeline(*m_outline);
    pass.BindFragmentTexture(0, *m_mask, *m_sampler);
    struct Settings {
        Color color;
        Vector2 texelSize;
        float width;
        float padding = 0;
    };
    pass.PushFragmentUniforms(0, Settings{{0.25f, 0.8f, 1.0f, 1}, {1.0f / m_size.x, 1.0f / m_size.y}, 2.0f});
    pass.Draw(3);
}
