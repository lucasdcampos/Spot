#pragma once

#include "graphics/camera_3d.h"
#include "graphics/color.h"
#include "graphics/light_data.h"
#include "graphics/mesh_data.h"
#include "graphics/render_context.h"
#include "graphics/sky_data.h"
#include "graphics/texture.h"
#include "math/matrix.h"
#include "math/vector.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Spot {

class GraphicsDevice;
class Mesh;
class Model;
class Pipeline;
class Scene;
struct Material;

namespace Detail {
struct SelectedLights;
struct ShadowUniforms;
struct SkyLighting;
} // namespace Detail

struct Renderer3DConfig {
    TextureFilter filter = TextureFilter::Linear;
    // See SamplerDesc::maxAnisotropy; 3D views see most surfaces at an angle.
    float maxAnisotropy = 16.0f;
    // The format of the textures it draws into; the window's swapchain when unset.
    std::optional<TextureFormat> targetFormat = std::nullopt;
    // The format of the depth target of the passes it draws in.
    TextureFormat depthFormat = TextureFormat::Depth32Float;
    // The sample count of the targets of the passes it draws in; above 1 for multisampled antialiasing.
    std::uint32_t sampleCount = 1;
    // Draws light as it is into an HDR target, for a tonemapper to bring within the display's range (see Tonemapper),
    // rather than mapping the sky's light itself, as it does into displayable targets.
    bool hdr = false;

    // The size in pixels of each cascade of the shadows of the directional light, which split the view up to the
    // shadow distance into 1 to 4 ranges, each sharper than the next: the cascades share one shadow map, and their
    // sizes take memory and time to draw. A size of 0 turns these shadows off.
    std::uint32_t shadowMapSize = 2048;
    std::uint32_t shadowCascadeCount = 4;
    // How far from the camera directional shadows reach; they fade out before it.
    float shadowDistance = 60.0f;
    // The size in pixels of each of the MaxShadowTiles tiles that point and spot light shadows are drawn in, which
    // are created the first time one is needed. 0 turns these shadows off.
    std::uint32_t localShadowMapSize = 512;
};

struct Renderer3DStats {
    // In the view's pass.
    std::uint32_t drawCalls = 0;
    std::uint32_t triangleCount = 0;
    // In the passes that draw shadow maps.
    std::uint32_t shadowDrawCalls = 0;
};

// Draws lit, textured meshes with depth testing. Like Renderer2D, it is built only on the public graphics API. It
// queues what is drawn between Begin() and End(), and End() opens the render pass described to Begin(), which must
// have a depth target, and draws everything in it:
//
//     renderer.Begin(context, {.clearColor = Spot::Color::Black, .depthTarget = depth.get(), .clearDepth = 1.0f},
//                    camera);
//     renderer.DrawLight({.type = Spot::LightType::Directional, .direction = {-0.4f, -1.0f, -0.6f}});
//     renderer.DrawSky({.sunDirection = {-0.4f, -1.0f, -0.6f}});
//     renderer.DrawModel(*model, Spot::Matrix4::Translation({0, 0, -5}));
//     renderer.End();
//
// Surfaces are opaque, and lit only by the lights drawn between Begin() and End(), so without any they are black.
// Ambient lights add up and are free; up to MaxLights others light each frame, and past that the directional ones and
// then those that reach closest to the camera are kept. Lights can cast shadows (LightData::castsShadows): the first
// such directional light, through shadow cascades, and point and spot lights, which take one tile each for a spot
// light and six for a point light, in the same order, as long as there are tiles left. Meshes, models, textures and the
// pass's targets must stay alive until End(), and no other pass can be open then.
class Renderer3D {
public:
    static constexpr std::size_t MaxLights = 16;
    // Shared by the shadows of point and spot lights.
    static constexpr std::size_t MaxShadowTiles = 32;

    explicit Renderer3D(GraphicsDevice& device, const Renderer3DConfig& config = {});
    ~Renderer3D();

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    // Whether its GPU resources were created; when not, drawing does nothing and the reason has been logged.
    bool IsValid() const;

    // The pass's target, or the window when it has none, sets the camera's aspect ratio.
    void Begin(RenderContext& context, const RenderPassDesc& target, const Camera3D& camera);
    void End();

    // Lights everything drawn in the frame, whether drawn before or after it.
    void DrawLight(const LightData& light);
    // Fills the view behind everything drawn in the frame with the sky, which also lights it, unless told not to: its
    // sun shines through the air, dimming and reddening as it nears the horizon and going out below it, and the rest
    // of the sky adds ambient light. So the sun is drawn with the sky, not with DrawLight(). A later call replaces it.
    // Without one, the pass's clear color shows.
    void DrawSky(const SkyData& sky);
    // Meshes that do not cast shadows are still shadowed by the others.
    void DrawMesh(const Mesh& mesh, const Material& material, const Matrix4& transform = {}, bool castsShadows = true);
    void DrawModel(const Model& model, const Matrix4& transform = {});
    // Draws every MeshRenderer and Light of the scene that is enabled on an active entity, at its entity's world
    // transform, and the first such Sky.
    // The scene must not change until End(), since its components keep the meshes alive.
    void DrawScene(Scene& scene);

    // What the last End() drew.
    Renderer3DStats GetStats() const { return m_stats; }

private:
    struct DrawCommand {
        const Mesh* mesh;
        const Texture* texture;
        Color color;
        bool doubleSided;
        float worldTextureSize;
        float specular;
        float shininess;
        Matrix4 transform;
        // Around the mesh, in world space.
        BoundingSphere bounds;
        bool castsShadows;
    };

    void DrawShadows(Detail::SelectedLights& lights, Detail::ShadowUniforms& shadows);
    // Whether the shadow map could be created.
    bool DrawCascades(const LightData& light, Detail::ShadowUniforms& shadows);
    void DrawLocalShadows(Detail::SelectedLights& lights, Detail::ShadowUniforms& shadows);
    void DrawShadowCasters(RenderPass& pass, const Matrix4& viewProjection, bool ignoreNear);
    void Flush(RenderPass& pass, const Detail::SelectedLights& lights, const Detail::ShadowUniforms& shadows);
    void FlushSky(RenderPass& pass, const SkyData& sky, const Detail::SkyLighting& lighting);

    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Pipeline> m_doubleSidedPipeline;
    std::unique_ptr<Pipeline> m_skyPipeline;
    std::unique_ptr<Pipeline> m_shadowPipeline;
    std::unique_ptr<Pipeline> m_doubleSidedShadowPipeline;
    std::unique_ptr<Sampler> m_sampler;
    std::unique_ptr<Sampler> m_shadowSampler;
    std::unique_ptr<Texture> m_whiteTexture;
    // Bound in place of shadow maps that are not drawn.
    std::unique_ptr<Texture> m_noShadows;
    bool m_noShadowsCleared = false;
    std::unique_ptr<Texture> m_cascadeShadows;
    std::unique_ptr<Texture> m_localShadows;
    GraphicsDevice* m_device;
    Renderer3DConfig m_config;

    std::vector<DrawCommand> m_commands;
    RenderContext* m_context = nullptr;
    RenderPassDesc m_target;
    std::vector<LightData> m_lights;
    std::optional<SkyData> m_sky;
    Matrix4 m_viewProjection;
    // Of the camera's rotation and projection only, which turns clip-space positions into directions.
    Matrix4 m_inverseSkyViewProjection;
    Camera3D m_camera;
    float m_aspectRatio = 1.0f;
    bool m_warnedAboutLights = false;
    Renderer3DStats m_stats;
};

} // namespace Spot
