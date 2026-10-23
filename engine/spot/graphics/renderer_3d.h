#pragma once

#include "spot/core/color.h"
#include "spot/core/matrix.h"
#include "spot/core/vector.h"
#include "spot/graphics/camera_3d.h"
#include "spot/graphics/custom_surface.h"
#include "spot/graphics/fog_data.h"
#include "spot/graphics/fullscreen_pass.h"
#include "spot/graphics/light_data.h"
#include "spot/graphics/material.h"
#include "spot/graphics/mesh.h"
#include "spot/graphics/mesh_data.h"
#include "spot/graphics/render_context.h"
#include "spot/graphics/sky_data.h"
#include "spot/graphics/texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Spot {

class GraphicsDevice;
class Model;
class ComputePipeline;
class Pipeline;
class RenderWorld;
struct Material;
struct RenderMeshObject;

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
    // In the view's pass. Meshes that share their mesh, material and pipeline are drawn together, as instances of one
    // draw call.
    std::uint32_t drawCalls = 0;
    std::uint32_t triangleCount = 0;
    // In the passes that draw shadow maps.
    std::uint32_t shadowDrawCalls = 0;
    // Meshes drawn in the view's pass, and meshes left out of it because they were outside the view. Meshes on layers
    // the camera does not see are not counted.
    std::uint32_t visibleMeshes = 0;
    std::uint32_t culledMeshes = 0;
    // That lit the view, ambient ones aside.
    std::uint32_t lights = 0;
    // How often the view's pass changed pipeline, and material (textures or values), between its draws; sorting keeps
    // both low.
    std::uint32_t pipelineChanges = 0;
    std::uint32_t materialChanges = 0;
    // 1 when the sky changed and its light was captured again, which takes a few passes.
    std::uint32_t skyLightingUpdates = 0;
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
    static constexpr std::size_t MaxLights = 256;
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
    // Renders the last submissions into readable single-sample camera depth, including cutouts.
    // Call after End(), before the next Begin(); transparent surfaces do not write depth.
    const Texture* RenderCameraDepth(RenderContext& context);

    // Lights everything drawn in the frame, whether drawn before or after it.
    void DrawLight(const LightData& light);
    // Fills the view behind everything drawn in the frame with the sky, which also lights it, unless told not to: its
    // sun shines through the air, dimming and reddening as it nears the horizon and going out below it, and the rest
    // of the sky adds ambient light. So the sun is drawn with the sky, not with DrawLight(). A later call replaces it.
    // Without one, the pass's clear color shows.
    void DrawSky(const SkyData& sky);
    // Fogs everything drawn in the frame, the sky included; a later call replaces it.
    void DrawFog(const FogData& fog);
    // Meshes that do not cast shadows are still shadowed by the others.
    // Draws one of the mesh's submeshes, or all of it. Transforms that mirror, with a negative scale, are drawn the
    // right way out.
    void DrawMesh(const Mesh& mesh, const Material& material, const Matrix4& transform = {}, bool castsShadows = true,
                  std::uint32_t submesh = Mesh::AllSubmeshes);
    // Draws each part of the model where its node puts it.
    void DrawSkinnedMesh(const Mesh& mesh, const Material& material, std::span<const Matrix4> palette,
                         const Matrix4& transform = {}, float boundsPadding = 1.0f, bool castsShadows = true,
                         std::uint32_t submesh = Mesh::AllSubmeshes);
    void DrawModel(const Model& model, const Matrix4& transform = {});
    // Draws every mesh of the world on a layer the camera sees. The world and what its objects show must stay alive
    // and unchanged until End().
    void DrawWorld(const RenderWorld& world);
    // Draws a surface of the game's own with its own shaders in the frame's passes: lit, shadowed and fogged like the
    // meshes, in their shadows and the camera's depth (see CustomSurface). It must stay alive until End(), and until
    // RenderCameraDepth() when that is called.
    void DrawSurface(CustomSurface& surface);
    // How Renderer3D draws its own shadow casters, which the pipelines of surfaces drawing into shadows match.
    static DepthState GetShadowDepthState();
    // The camera Begin() was given.
    const Camera3D& GetCamera() const { return m_camera; }

    // What the last End() drew.
    Renderer3DStats GetStats() const { return m_stats; }

private:
    // The material's textures, in the order the mesh shader binds them after the shadow maps.
    using MaterialTextures = std::array<const Texture*, 5>;
    // As the mesh shaders read each instance.
    struct InstanceData {
        Matrix4 model;
        Matrix4 normalMatrix;
        std::array<std::uint32_t, 4> skin;
        Vector4 bounds;
    };

    struct DrawCommand {
        const Mesh* mesh;
        // Base color, metallic-roughness, normal, occlusion and emissive; null where the material has none.
        MaterialTextures textures;
        Color color;
        float metallic;
        float roughness;
        float normalScale;
        float occlusionStrength;
        Color emissive;
        AlphaMode alphaMode;
        float alphaCutoff;
        bool doubleSided;
        float worldTextureSize;
        Matrix4 transform;
        // Around the mesh, in world space.
        BoundingSphere bounds;
        bool castsShadows;
        // Whether it is in the camera's view, which End() decides.
        bool visible = true;
        // Of its bounds' center along the camera's forward axis.
        float depth = 0.0f;
        // The same for commands whose material looks the same, textures and values alike, which End() gives.
        std::uint32_t look = 0;
        // The range of the mesh's indices it draws.
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        // Whether its transform turns the mesh inside out, which swaps the faces culled.
        bool mirrored = false;
        std::uint32_t skinOffset = 0;
        std::uint32_t skinCount = 0;
        // For a batch of a render world kept on the GPU: its instances, from the first on, which the shaders cull
        // themselves. The command's bounds are then around all of them.
        const Buffer* instanceBuffer = nullptr;
        std::uint32_t instanceFirst = 0;
        std::uint32_t instanceCount = 1;
        // The batch's instances as uploaded, which shadow views cull one by one.
        const InstanceData* instanceData = nullptr;
        // When the culling pass ran for the frame: the instances it kept, and the batch's indirect draw in arguments,
        // the batch-th.
        const Buffer* keptInstances = nullptr;
        const Buffer* arguments = nullptr;
        std::uint32_t batch = 0;
        bool nearestFiltering = false;
    };

    // A render world as kept on the GPU: its objects' instances, in batches of those drawn the same way, so drawing
    // it costs a command per batch, and only what changed is uploaded again.
    struct WorldBatch {
        const Mesh* mesh;
        const Material* material;
        std::uint32_t firstIndex;
        std::uint32_t indexCount;
        bool mirrored;
        bool castsShadows;
        std::uint32_t layer;
        // Of its instances, in the world's buffer.
        std::uint32_t first;
        std::uint32_t count;
        BoundingSphere bounds;
        bool boundsChanged;
    };
    struct WorldCache {
        std::uint64_t worldId = 0;
        std::uint64_t structureVersion = 0;
        std::uint64_t changeCount = 0;
        bool built = false;
        std::unique_ptr<Buffer> instances;
        std::vector<InstanceData> instanceData;
        // The world's object and level of detail at each instance, and the instances of each object: those in
        // slots from slotStarts[object] to slotStarts[object + 1].
        std::vector<std::uint32_t> objects;
        std::vector<std::uint32_t> levels;
        std::vector<std::uint32_t> slotStarts;
        std::vector<std::uint32_t> slots;
        std::vector<std::uint32_t> batchOfInstance;
        std::vector<WorldBatch> batches;
        std::uint64_t lastDrawn = 0;
        // For the culling pass: each batch's first instance, count, index count and first index, the instances it
        // keeps, and the batches' indirect draws.
        std::unique_ptr<Buffer> batchTable;
        std::unique_ptr<Buffer> visible;
        std::unique_ptr<Buffer> arguments;
    };
    // Culls the instances of the frame's worlds on the GPU, against the view, their levels of detail and what the
    // last frame of the view drew, into lists the view's pass draws indirectly.
    void CullWorlds();
    // Keeps the farthest depth of each tile of what the view's pass drew, for the next frame to cull against.
    void UpdateOcclusionTiles();
    // Null when its buffer cannot be made, after logging why.
    WorldCache* UpdateWorldCache(const RenderWorld& world);

    // Draws the level of detail of the object.
    void AddCommand(const RenderMeshObject& object, const Material& material, std::uint32_t level = 0);
    std::uint32_t PrepareSurfaces(RenderContext& context, SurfacePass kind, const Matrix4& viewProjection,
                                  bool ignoreNear);
    void DrawSurfaces(RenderPass& pass, SurfacePass kind, const Matrix4& viewProjection, bool ignoreNear,
                      std::uint32_t viewIndex);
    void BindLighting(RenderPass& pass, const Detail::SelectedLights& lights, const Detail::ShadowUniforms& shadows);
    void DrawShadows(Detail::SelectedLights& lights, Detail::ShadowUniforms& shadows);
    void DrawShadowViews(Detail::SelectedLights& lights, Detail::ShadowUniforms& shadows);
    // Whether the shadow map could be created.
    bool DrawCascades(const LightData& light, Detail::ShadowUniforms& shadows);
    void DrawLocalShadows(Detail::SelectedLights& lights, Detail::ShadowUniforms& shadows);
    void DrawShadowCasters(RenderPass& pass, const Matrix4& viewProjection, bool ignoreNear);
    // Gives the commands their looks.
    void AssignLooks();
    // Sends every command's transforms to the instance buffer, in the commands' order.
    bool UploadInstances();
    // Sends the lights and the view's clusters of them to their buffers.
    bool UploadLights(const Detail::SelectedLights& lights);
    void Flush(RenderPass& pass);
    const Pipeline* GetPipeline(const DrawCommand& command) const;
    const Pipeline* GetShadowPipeline(const DrawCommand& command) const;
    void FlushSky(RenderPass& pass, const SkyData& sky, const Detail::SkyLighting& lighting);
    // Captures the sky's light into the reflections and irradiance when it changed; false when it cannot.
    bool UpdateSkyLighting(const SkyData& sky, const Detail::SkyLighting& lighting);

    std::array<std::unique_ptr<Pipeline>, 12> m_cameraDepthPipelines;
    std::array<std::unique_ptr<Pipeline>, 6> m_skinnedPipelines;
    std::array<std::unique_ptr<Pipeline>, 6> m_skinnedShadowPipelines;
    std::unique_ptr<Buffer> m_palettes;
    std::vector<Matrix4> m_paletteData;
    std::unique_ptr<Texture> m_cameraDepth;
    std::unique_ptr<Buffer> m_cameraDepthIndices;
    std::vector<std::uint32_t> m_cameraDepthOrder;
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Pipeline> m_doubleSidedPipeline;
    std::unique_ptr<Pipeline> m_transparentPipeline;
    std::unique_ptr<Pipeline> m_doubleSidedTransparentPipeline;
    // Single-sided ones for mirroring transforms, which cull front faces, as the mirror winds them clockwise.
    std::unique_ptr<Pipeline> m_mirroredPipeline;
    std::unique_ptr<Pipeline> m_mirroredTransparentPipeline;
    std::unique_ptr<Pipeline> m_mirroredShadowPipeline;
    std::unique_ptr<Pipeline> m_mirroredCutoutShadowPipeline;
    std::unique_ptr<Pipeline> m_skyPipeline;
    std::unique_ptr<Pipeline> m_shadowPipeline;
    std::unique_ptr<Pipeline> m_doubleSidedShadowPipeline;
    std::unique_ptr<Pipeline> m_cutoutShadowPipeline;
    std::unique_ptr<Pipeline> m_doubleSidedCutoutShadowPipeline;
    std::unique_ptr<Sampler> m_sampler;
    std::unique_ptr<Sampler> m_nearestSampler;
    std::unique_ptr<Sampler> m_shadowSampler;
    std::unique_ptr<Texture> m_whiteTexture;
    // Bound in place of shadow maps that are not drawn.
    std::unique_ptr<Texture> m_noShadows;
    bool m_noShadowsCleared = false;
    std::unique_ptr<Texture> m_cascadeShadows;
    std::unique_ptr<Texture> m_localShadows;
    // The transforms of every command of the frame, which instanced draws read from the first of their batch on.
    std::unique_ptr<Buffer> m_instances;
    // The commands each shadow view draws, as indices into the instances, uploaded anew for each view.
    std::unique_ptr<Buffer> m_shadowCasters;
    // The commands that may cast shadows, in the order that keeps those that cast the same shadow side by side, which
    // each shadow view keeps those it sees of.
    std::vector<std::uint32_t> m_shadowOrder;
    // The casters of every shadow view of the frame, one view after the other, as indices into the instances.
    std::vector<std::uint32_t> m_frameCasters;
    // The frame's lights, the view's clusters and their lists of lights.
    std::unique_ptr<Buffer> m_lightBuffer;
    std::unique_ptr<Buffer> m_clusterBuffer;
    std::unique_ptr<Buffer> m_lightIndexBuffer;
    std::uint32_t m_directionalLightCount = 0;
    float m_clusterNear = 0.1f;
    float m_clusterScale = 1.0f;
    Vector2 m_viewSize = {1.0f, 1.0f};
    // The sky's light, as surfaces show it: captured into a cube, then blurred for each roughness down the mip levels
    // of the reflections, and projected onto spherical harmonics in the irradiance. Black ones stand in without a
    // sky.
    std::unique_ptr<Pipeline> m_skyCapturePipeline;
    std::unique_ptr<FullscreenPass> m_skyPrefilter;
    std::unique_ptr<FullscreenPass> m_skyIrradiancePass;
    std::unique_ptr<Sampler> m_skySampler;
    std::unique_ptr<Texture> m_skyEnvironment;
    std::unique_ptr<Texture> m_skyReflections;
    std::unique_ptr<Texture> m_skyIrradiance;
    std::unique_ptr<Texture> m_noSkyReflections;
    std::unique_ptr<Texture> m_noSkyIrradiance;
    std::optional<SkyData> m_capturedSky;
    bool m_skyLit = false;
    GraphicsDevice* m_device;
    Renderer3DConfig m_config;

    std::vector<DrawCommand> m_commands;
    // The frame's custom surfaces, kept until the next Begin() for RenderCameraDepth().
    std::vector<CustomSurface*> m_surfaces;
    std::uint32_t m_surfaceViewIndex = 0;
    std::vector<std::unique_ptr<WorldCache>> m_worldCaches;
    // Occlusion culling: the last frame's depth, by tiles, and the view it was drawn from, which only the next frame
    // of the same view culls against.
    std::unique_ptr<FullscreenPass> m_occlusionTilePass;
    std::unique_ptr<ComputePipeline> m_occlusionPipeline;
    std::unique_ptr<Sampler> m_occlusionSampler;
    std::unique_ptr<Texture> m_occlusionTiles;
    std::unique_ptr<Buffer> m_noVisible;
    // Stands in for tiles when there are none, hiding nothing.
    std::unique_ptr<Texture> m_noOcclusionTiles;
    Matrix4 m_occlusionViewProjection;
    const Texture* m_occlusionDepth = nullptr;
    bool m_occlusionValid = false;
    // Counts End() calls, so worlds no longer drawn are let go.
    std::uint64_t m_endCount = 0;
    // What End() sorts the commands by, small enough to move around cheaply, and where they go once sorted.
    struct SortEntry {
        bool transparent;
        bool hidden;
        bool doubleSided;
        bool mirrored;
        AlphaMode alphaMode;
        std::uint32_t look;
        const Mesh* mesh;
        std::uint32_t firstIndex;
        // Along the camera's forward axis for surfaces that hide what is behind; the squared distance from the eye
        // for transparent ones.
        float depth;
        std::uint32_t index;
    };
    std::vector<SortEntry> m_sortEntries;
    std::vector<DrawCommand> m_sortedCommands;
    RenderContext* m_context = nullptr;
    RenderPassDesc m_target;
    std::vector<LightData> m_lights;
    std::optional<SkyData> m_sky;
    std::optional<FogData> m_fog;
    Matrix4 m_viewProjection;
    // Of the camera's rotation and projection only, which turns clip-space positions into directions.
    Matrix4 m_inverseSkyViewProjection;
    Camera3D m_camera;
    float m_aspectRatio = 1.0f;
    bool m_warnedAboutLights = false;
    Renderer3DStats m_stats;
};

} // namespace Spot
