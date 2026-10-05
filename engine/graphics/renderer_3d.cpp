#include "graphics/renderer_3d.h"

#include "core/assert.h"
#include "core/log.h"
#include "graphics/atmosphere.h"
#include "graphics/graphics_device.h"
#include "graphics/material.h"
#include "graphics/mesh.h"
#include "graphics/model.h"
#include "graphics/render_context.h"
#include "scene/light.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"
#include "scene/sky.h"

#include "mesh.frag.h"
#include "mesh.vert.h"
#include "sky.frag.h"
#include "sky.vert.h"

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
    Vector4 cameraPosition;
    // x is the number of lights; a whole row keeps the layout the same in every shader language.
    std::array<std::uint32_t, 4> lightCount;
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
};

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

struct MaterialUniforms {
    Color baseColor;
    float doubleSided;
    float worldTextureSize;
    float specular;
    float shininess;
};

// Ambient lights are summed. Past MaxLights, the other lights that matter least to what is near the camera are dropped.
LightingUniforms GetLightingUniforms(const std::vector<LightData>& allLights, Vector3 cameraPosition,
                                     bool& warnedAboutLights) {
    LightingUniforms uniforms{};
    uniforms.ambient.a = 1.0f;
    uniforms.cameraPosition = {cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f};

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
    return m_pipeline && m_doubleSidedPipeline && m_skyPipeline && m_sampler && m_whiteTexture;
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
    m_cameraPosition = camera.position;
    m_commands.clear();
    m_lights.clear();
    m_sky.reset();
}

void Renderer3D::End() {
    SPOT_ASSERT(m_context, "Renderer3D::End() called without Begin()");
    auto pass = m_context->BeginRenderPass(m_target);
    Flush(pass);
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

void Renderer3D::DrawMesh(const Mesh& mesh, const Material& material, const Matrix4& transform) {
    SPOT_ASSERT(m_context, "Draw calls must come between Begin() and End()");
    m_commands.push_back({&mesh, material.baseColorTexture.Get(), material.baseColor, material.doubleSided,
                          material.worldTextureSize, material.specular, material.shininess, transform});
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
    scene.ForEachComponent<MeshRenderer>([this](const MeshRenderer& renderer) {
        if (renderer.GetMesh()) {
            DrawMesh(*renderer.GetMesh(), renderer.GetMaterial() ? *renderer.GetMaterial() : DefaultMaterial,
                     renderer.GetEntity().GetTransform().GetWorldMatrix());
        }
    });
}

void Renderer3D::FlushSky(RenderPass& pass, const SkyData& sky, const SkyLighting& lighting) {
    const Vector3 toSun = -Normalize(sky.sunDirection);
    const bool hasSun = sky.hasSun && toSun != Vector3{};
    const float intensity = hasSun ? sky.sunIntensity : 0.0f;
    // The ground below the horizon is lit like a surface of the scene facing up.
    Color groundLight = lighting.ambient;
    if (lighting.sun) {
        groundLight = groundLight + lighting.sun->color * std::max(toSun.y, 0.0f);
    }

    pass.BindPipeline(*m_skyPipeline);
    pass.PushVertexUniforms(0, m_inverseSkyViewProjection);
    pass.PushFragmentUniforms(
        0, SkyUniforms{
               .sunDirection = {toSun.x, toSun.y, toSun.z, hasSun ? 1.0f : 0.0f},
               .sunColor = {sky.sunColor.r * intensity, sky.sunColor.g * intensity, sky.sunColor.b * intensity, 1.0f},
               .skyTint = sky.skyTint,
               .groundColor = sky.groundColor,
               .groundLight = groundLight,
               .sunLight = lighting.sun ? lighting.sun->color : Color::Black,
               .nightColor = sky.nightColor,
               .settings = {std::max(sky.sunSize, 0.0f), std::max(sky.atmosphereThickness, 0.0f),
                            std::max(sky.exposure, 0.0f), 0.0f},
           });
    pass.Draw(3);
    ++m_stats.drawCalls;
    ++m_stats.triangleCount;
}

void Renderer3D::Flush(RenderPass& pass) {
    m_stats = {};
    if (!IsValid()) {
        return;
    }
    if (m_sky) {
        const SkyLighting lighting = GetSkyLighting(*m_sky);
        FlushSky(pass, *m_sky, lighting);
        if (m_sky->lightsScene) {
            if (lighting.sun) {
                m_lights.push_back(*lighting.sun);
            }
            m_lights.push_back({.type = LightType::Ambient, .color = lighting.ambient});
        }
    }
    if (m_commands.empty()) {
        return;
    }

    // Surfaces are opaque and depth tested, so the order only matters for how often state changes.
    std::stable_sort(m_commands.begin(), m_commands.end(), [](const DrawCommand& a, const DrawCommand& b) {
        return std::tie(a.doubleSided, a.texture) < std::tie(b.doubleSided, b.texture);
    });

    pass.PushVertexUniforms(0, m_viewProjection);
    pass.PushFragmentUniforms(0, GetLightingUniforms(m_lights, m_cameraPosition, m_warnedAboutLights));

    const Pipeline* boundPipeline = nullptr;
    const Texture* boundTexture = nullptr;
    for (const auto& command : m_commands) {
        const Pipeline* pipeline = command.doubleSided ? m_doubleSidedPipeline.get() : m_pipeline.get();
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
        }
        const Texture* texture = command.texture ? command.texture : m_whiteTexture.get();
        if (texture != boundTexture) {
            pass.BindFragmentTexture(0, *texture, *m_sampler);
            boundTexture = texture;
        }

        pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
        pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
        pass.PushVertexUniforms(1, ObjectUniforms{command.transform, Transpose(Inverse(command.transform))});
        pass.PushFragmentUniforms(1, MaterialUniforms{.baseColor = command.color,
                                                      .doubleSided = command.doubleSided ? 1.0f : 0.0f,
                                                      .worldTextureSize = command.worldTextureSize,
                                                      .specular = command.specular,
                                                      .shininess = command.shininess});
        pass.DrawIndexed(command.mesh->GetIndexCount());

        ++m_stats.drawCalls;
        m_stats.triangleCount += command.mesh->GetIndexCount() / 3;
    }
}

} // namespace Spot
