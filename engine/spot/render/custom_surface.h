#pragma once

#include "spot/core/math/matrix.h"
#include "spot/core/math/vector.h"
#include "spot/graphics/pipeline.h"
#include "spot/graphics/texture.h"
#include "spot/render/camera_3d.h"

#include <array>
#include <cstdint>
#include <optional>

namespace Spot {

class GraphicsDevice;
class RenderPass;
class RenderContext;

// Which of Renderer3D's passes a custom surface draws in.
enum class SurfacePass : std::uint8_t {
    // The view's pass, with the frame's lights, shadows, sky and fog bound.
    Lit,
    // A view of a light into its shadow map: only depth.
    Shadow,
    // The camera's depth alone, as Renderer3D::RenderCameraDepth() draws it for effects that read it.
    Depth,
};

// Called with no pass open, so a surface can run compute before the view reads its buffers. Several shadow views
// may be prepared before any are drawn; keep their output separate, addressed by viewIndex until DrawShadow().
struct SurfacePrepareContext {
    GraphicsDevice& device;
    RenderContext& context;
    SurfacePass kind;
    const Camera3D& camera;
    Matrix4 viewProjection;
    std::array<Vector4, 6> planes;
    // Unique within this Renderer3D::Begin()/End() and subsequent camera-depth draws; starts over at Begin().
    std::uint32_t viewIndex;
};

// What a custom surface draws with in one of Renderer3D's passes.
struct SurfaceDrawContext {
    GraphicsDevice& device;
    RenderPass& pass;
    SurfacePass kind;
    // The camera the frame is seen through, whichever view this pass is.
    const Camera3D& camera;
    // The view this pass draws: the camera's, or a light's for shadows.
    Matrix4 viewProjection;
    // The planes of that view, normals inward, with their distance from the origin in w; see IsInView().
    std::array<Vector4, 6> planes;

    // What pipelines drawing in the pass must match. Lit passes have a color and a depth target; shadow and depth
    // passes only depth (PipelineDesc::depthOnly), and shadows clamp depth and bias it as Renderer3D's own casters do.
    std::optional<TextureFormat> colorFormat;
    std::uint32_t sampleCount;
    DepthState depth;
    // Matches the Prepare() call that preceded this view.
    std::uint32_t viewIndex = 0;

    // Whether a sphere may show in the view, for surfaces culling their parts.
    bool IsInView(Vector3 center, float radius) const {
        for (const Vector4& plane : planes) {
            if (plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w < -radius) {
                return false;
            }
        }
        return true;
    }
};

// Geometry of a game's own, drawn by its own shaders inside Renderer3D's passes, so it is lit, shadowed and fogged as
// the engine's meshes are, casts shadows on them, and hides what is behind it from occlusion culling: terrain, water,
// or anything kept in buffers of its own. Renderer3D::DrawSurface() hands one to the frame.
//
// In the lit pass, Renderer3D has bound the view (vertex uniforms at b0 of space1, see surface_frame.hlsl) and the
// lighting (see surface_lighting.hlsl, whose ShadeOpaqueSurface() shades a point as the engine's meshes are shaded)
// before the surface draws. The surface binds its own pipeline, buffers, the textures of its own slots and its own
// uniforms; what it binds does not outlast its turn. Surfaces draw after the sky, before Renderer3D's meshes.
class CustomSurface {
public:
    virtual ~CustomSurface() = default;

    virtual void Prepare(const SurfacePrepareContext& /*context*/) {}
    virtual void DrawLit(const SurfaceDrawContext& context) = 0;
    // Each view of a shadow-casting light; surfaces that cast no shadow draw nothing.
    virtual void DrawShadow(const SurfaceDrawContext& /*context*/) {}
    // The camera's depth, for effects that read it after the frame.
    virtual void DrawDepth(const SurfaceDrawContext& /*context*/) {}
};

} // namespace Spot
