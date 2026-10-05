#include "graphics/renderer_3d.h"

#include "core/assert.h"
#include "core/log.h"
#include "graphics/graphics_device.h"
#include "graphics/material.h"
#include "graphics/mesh.h"
#include "graphics/model.h"
#include "graphics/render_context.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"

#include "mesh.frag.h"
#include "mesh.vert.h"

#include <algorithm>
#include <array>
#include <tuple>

namespace Spot {

namespace {

// These mirror the shaders' constant buffers.
struct ObjectUniforms {
    Matrix4 model;
    Matrix4 normalMatrix;
};

struct LightingUniforms {
    Vector4 direction;
    Color color;
    Color ambient;
};

struct MaterialUniforms {
    Color baseColor;
    float doubleSided;
    // Constant buffers are sized in 16-byte rows.
    std::array<float, 3> padding;
};

} // namespace

Renderer3D::Renderer3D(GraphicsDevice& device, const Renderer3DConfig& config) {
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
        };
        m_pipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::None;
        m_doubleSidedPipeline = device.CreatePipeline(desc);
    }

    m_sampler = device.CreateSampler({.filter = config.filter, .addressMode = TextureAddressMode::Repeat});

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
    return m_pipeline && m_doubleSidedPipeline && m_sampler && m_whiteTexture;
}

void Renderer3D::Begin(RenderPass& pass, const Camera3D& camera, Vector2 viewSize, const Lighting& lighting) {
    SPOT_ASSERT(!m_pass, "Renderer3D::Begin() called twice without End()");
    m_pass = &pass;
    m_viewProjection = camera.GetViewProjection(viewSize);
    m_lighting = lighting;
    m_commands.clear();
}

void Renderer3D::End() {
    SPOT_ASSERT(m_pass, "Renderer3D::End() called without Begin()");
    Flush();
    m_pass = nullptr;
}

void Renderer3D::DrawMesh(const Mesh& mesh, const Material& material, const Matrix4& transform) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    m_commands.push_back({&mesh, material.baseColorTexture.Get(), material.baseColor, material.doubleSided, transform});
}

void Renderer3D::DrawModel(const Model& model, const Matrix4& transform) {
    // Parts whose mesh failed to load are skipped; without a material they get the default one.
    static const Material DefaultMaterial;
    for (const auto& part : model.GetParts()) {
        if (part.mesh) {
            DrawMesh(*part.mesh, part.material ? *part.material : DefaultMaterial, transform);
        }
    }
}

void Renderer3D::DrawScene(Scene& scene) {
    scene.ForEachComponent<MeshRenderer>([this](const MeshRenderer& renderer) {
        if (renderer.GetMesh()) {
            DrawMesh(*renderer.GetMesh(), renderer.GetMaterial(), renderer.GetEntity().GetTransform().GetWorldMatrix());
        }
    });
}

void Renderer3D::Flush() {
    m_stats = {};
    if (m_commands.empty() || !IsValid()) {
        return;
    }

    // Surfaces are opaque and depth tested, so the order only matters for how often state changes.
    std::stable_sort(m_commands.begin(), m_commands.end(), [](const DrawCommand& a, const DrawCommand& b) {
        return std::tie(a.doubleSided, a.texture) < std::tie(b.doubleSided, b.texture);
    });

    const Vector3 direction = Normalize(m_lighting.direction);
    m_pass->PushVertexUniforms(0, m_viewProjection);
    m_pass->PushFragmentUniforms(0, LightingUniforms{.direction = {direction.x, direction.y, direction.z, 0.0f},
                                                     .color = m_lighting.color,
                                                     .ambient = m_lighting.ambient});

    const Pipeline* boundPipeline = nullptr;
    const Texture* boundTexture = nullptr;
    for (const auto& command : m_commands) {
        const Pipeline* pipeline = command.doubleSided ? m_doubleSidedPipeline.get() : m_pipeline.get();
        if (pipeline != boundPipeline) {
            m_pass->BindPipeline(*pipeline);
            boundPipeline = pipeline;
        }
        const Texture* texture = command.texture ? command.texture : m_whiteTexture.get();
        if (texture != boundTexture) {
            m_pass->BindFragmentTexture(0, *texture, *m_sampler);
            boundTexture = texture;
        }

        m_pass->BindVertexBuffer(0, command.mesh->GetVertexBuffer());
        m_pass->BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
        m_pass->PushVertexUniforms(1, ObjectUniforms{command.transform, Transpose(Inverse(command.transform))});
        m_pass->PushFragmentUniforms(1, MaterialUniforms{.baseColor = command.color,
                                                         .doubleSided = command.doubleSided ? 1.0f : 0.0f,
                                                         .padding = {}});
        m_pass->DrawIndexed(command.mesh->GetIndexCount());

        ++m_stats.drawCalls;
        m_stats.triangleCount += command.mesh->GetIndexCount() / 3;
    }
}

} // namespace Spot
