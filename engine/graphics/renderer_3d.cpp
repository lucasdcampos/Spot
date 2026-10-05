#include "graphics/renderer_3d.h"

#include "core/assert.h"
#include "core/log.h"
#include "graphics/graphics_device.h"
#include "graphics/material.h"
#include "graphics/mesh.h"
#include "graphics/model.h"
#include "graphics/render_context.h"
#include "scene/light.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"

#include "mesh.frag.h"
#include "mesh.vert.h"

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
    // The cosine of the outer cone angle, and the inverse of how much greater the inner one's is.
    Vector4 cone;
};

struct LightingUniforms {
    Color ambient;
    // x is the number of lights; a whole row keeps the layout the same in every shader language.
    std::array<std::uint32_t, 4> lightCount;
    std::array<LightUniforms, Renderer3D::MaxLights> lights;
};

struct MaterialUniforms {
    Color baseColor;
    float doubleSided;
    float worldTextureSize;
    // Constant buffers are sized in 16-byte rows.
    std::array<float, 2> padding;
};

// Ambient lights are summed. Past MaxLights, the other lights that matter least to what is near the camera are dropped.
LightingUniforms GetLightingUniforms(const std::vector<LightData>& allLights, Vector3 cameraPosition,
                                     bool& warnedAboutLights) {
    LightingUniforms uniforms{};
    uniforms.ambient.a = 1.0f;

    const auto rank = [cameraPosition](const LightData& light) {
        return light.type == LightType::Directional
                   ? -1.0f
                   : std::max(Distance(light.position, cameraPosition) - light.range, 0.0f);
    };
    std::vector<const LightData*> lights;
    for (const auto& light : allLights) {
        if (light.type == LightType::Ambient) {
            uniforms.ambient.r += light.color.r * light.intensity;
            uniforms.ambient.g += light.color.g * light.intensity;
            uniforms.ambient.b += light.color.b * light.intensity;
        } else {
            lights.push_back(&light);
        }
    }
    if (lights.size() > Renderer3D::MaxLights) {
        if (!warnedAboutLights) {
            Log::GetEngineLogger().Warning(
                "Renderer3D was given {} lights in a frame, more than the {} it can use; the "
                "farthest are left out",
                lights.size(), Renderer3D::MaxLights);
            warnedAboutLights = true;
        }
        std::ranges::stable_sort(lights, {}, [&](const LightData* light) { return rank(*light); });
        lights.resize(Renderer3D::MaxLights);
    }

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
            .cone = {outerCos, 1.0f / (innerCos - outerCos), 0.0f, 0.0f},
        };
    }
    return uniforms;
}

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
            .sampleCount = config.sampleCount,
        };
        m_pipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::None;
        m_doubleSidedPipeline = device.CreatePipeline(desc);
    }

    m_sampler = device.CreateSampler(
        {.filter = config.filter, .addressMode = TextureAddressMode::Repeat, .maxAnisotropy = config.maxAnisotropy});

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

void Renderer3D::Begin(RenderPass& pass, const Camera3D& camera, Vector2 viewSize) {
    SPOT_ASSERT(!m_pass, "Renderer3D::Begin() called twice without End()");
    m_pass = &pass;
    m_viewProjection = camera.GetViewProjection(viewSize);
    m_cameraPosition = camera.position;
    m_commands.clear();
    m_lights.clear();
}

void Renderer3D::End() {
    SPOT_ASSERT(m_pass, "Renderer3D::End() called without Begin()");
    Flush();
    m_pass = nullptr;
}

void Renderer3D::DrawLight(const LightData& light) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    m_lights.push_back(light);
}

void Renderer3D::DrawMesh(const Mesh& mesh, const Material& material, const Matrix4& transform) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    m_commands.push_back({&mesh, material.baseColorTexture.Get(), material.baseColor, material.doubleSided,
                          material.worldTextureSize, transform});
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
    static const Material DefaultMaterial;
    scene.ForEachComponent<Light>([this](const Light& light) { DrawLight(light.GetLightData()); });
    scene.ForEachComponent<MeshRenderer>([this](const MeshRenderer& renderer) {
        if (renderer.GetMesh()) {
            DrawMesh(*renderer.GetMesh(), renderer.GetMaterial() ? *renderer.GetMaterial() : DefaultMaterial,
                     renderer.GetEntity().GetTransform().GetWorldMatrix());
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

    m_pass->PushVertexUniforms(0, m_viewProjection);
    m_pass->PushFragmentUniforms(0, GetLightingUniforms(m_lights, m_cameraPosition, m_warnedAboutLights));

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
                                                         .worldTextureSize = command.worldTextureSize,
                                                         .padding = {}});
        m_pass->DrawIndexed(command.mesh->GetIndexCount());

        ++m_stats.drawCalls;
        m_stats.triangleCount += command.mesh->GetIndexCount() / 3;
    }
}

} // namespace Spot
