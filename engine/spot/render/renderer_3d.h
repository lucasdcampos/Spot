#pragma once

#include "spot/core/math/color.h"
#include "spot/core/math/matrix.h"
#include "spot/core/math/vector.h"
#include "spot/graphics/mesh_data.h"
#include "spot/graphics/render_context.h"
#include "spot/graphics/texture.h"
#include "spot/render/ambient_occlusion.h"
#include "spot/render/camera_3d.h"
#include "spot/render/custom_surface.h"
#include "spot/render/fog_data.h"
#include "spot/render/light_data.h"
#include "spot/render/material.h"
#include "spot/render/mesh.h"
#include "spot/render/reflection_probe_data.h"
#include "spot/render/sky_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Spot {

class GraphicsDevice;
class Model;
class RenderWorld;
struct Material;

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

    // Keeps the render worlds it draws on the GPU, in batches of nearby objects drawn the same way, whose instances
    // the GPU culls against the view and by level of detail, and, where the device has compute and indirect draws,
    // against what the last frame of the view hid. For worlds of many objects, where it spares the CPU a command per
    // object; off, worlds are drawn object by object, culled and given their levels of detail on the CPU.
    bool gpuCulling = false;

    // Draws the depth of the opaque and cutout meshes in view before the view's pass, which then shades only what
    // shows, each pixel once, rather than every surface drawn over it, and draws the sky only where nothing hides it.
    // It costs a pass over their vertices, which surfaces covering each other repay, as in most 3D views; the image is
    // the same either way.
    bool depthPrepass = true;

    // Darkens the light from all around in the creases and corners of what the camera sees, worked out from the
    // camera's depth, which costs a depth pass over the view and a few passes over its pixels (see AmbientOcclusion).
    AmbientOcclusionSettings ambientOcclusion = {};
    // Draws the sky's clouds in the main view as a volume, thickness tall, which a camera can fly through and which
    // mountains rise into, rather than as a layer: marched through at half the view's size against the camera's depth,
    // which costs a depth pass when ambient occlusion did not draw it, and evened out over frames. HDR targets only;
    // reflections still see the layer.
    bool volumetricClouds = false;
    // The size in pixels of the sky's occlusion: the depth of the highest surfaces around the camera seen from above,
    // drawn from the shadow casters, which keeps the sky's light from what lies under cover, as rooms, caves and the
    // ground under trees, and costs a pass over the casters around the camera. 0 turns it off, and everything is lit
    // by the sky as if in the open.
    std::uint32_t skyOcclusionSize = 0;
    // How far it reaches from the camera along the ground, and up and down.
    float skyOcclusionDistance = 64.0f;
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
    // The reflection probes that lit the view, and 1 when one was captured, which draws the scene six times.
    std::uint32_t reflectionProbes = 0;
    std::uint32_t reflectionProbeCaptures = 0;
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
//
// The light from all around comes from ambient lights, the sky and reflection probes: where a probe's box holds a
// surface, its capture of the scene lights it in place of the sky; elsewhere the sky does, as much as the sky's
// occlusion finds the surface in the open (Renderer3DConfig::skyOcclusionSize). Ambient occlusion darkens it in
// creases (Renderer3DConfig::ambientOcclusion).
class Renderer3D {
public:
    static constexpr std::size_t MaxLights = 256;
    // Shared by the shadows of point and spot lights.
    static constexpr std::size_t MaxShadowTiles = 32;
    // That light a frame; past them, those farthest from the camera are left out.
    static constexpr std::size_t MaxReflectionProbes = 16;

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
    // Call after End(), before the next Begin(); transparent surfaces do not write depth. With ambient occlusion,
    // End() drew it already, and it is given as it is.
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
    // Lights what is inside the probe's box with its capture of the scene, from this frame on when it was captured
    // before, or once End() captured it: one probe a frame, the nearest the camera first, drawing everything drawn in
    // the frame six times, around the probe. See ReflectionProbeData.
    void DrawReflectionProbe(const ReflectionProbeData& probe);
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
    const Camera3D& GetCamera() const;

    // What the last End() drew.
    Renderer3DStats GetStats() const;

    // Engine-internal: the parts it draws with (see render/renderer_3d_internal.h).
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
