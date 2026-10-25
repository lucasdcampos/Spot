#pragma once

#include "spot/graphics/texture.h"
#include "spot/render/camera_3d.h"
#include "spot/render/fullscreen_pass.h"

#include <memory>

namespace Spot {

class GraphicsDevice;
class RenderContext;

struct AmbientOcclusionSettings {
    // Darkens the light from all around in the creases and corners of what the camera sees; off, surfaces are darkened
    // only by their materials' occlusion maps.
    bool enabled = false;
    // How far apart, in world units, surfaces still hide light from each other: larger darkens wide creases, smaller
    // only tight corners and where things touch.
    float radius = 0.5f;
    // How dark what is hidden gets; 0 is not at all.
    float intensity = 1.0f;

    bool operator==(const AmbientOcclusionSettings&) const = default;
};

// Ambient occlusion from a camera's depth: how much of the light from all around each pixel's surface the surfaces near
// it hide, which darkens creases, corners and where things touch, as light bounces less into them. It works from
// depth alone, so it reaches only what the camera sees: a surface hidden behind another hides nothing. Renderer3D
// draws it before its pass when Renderer3DConfig::ambientOcclusion says so. Built on FullscreenPass.
class AmbientOcclusion {
public:
    // Null, after logging why, when its resources cannot be created.
    static std::unique_ptr<AmbientOcclusion> Create(GraphicsDevice& device);

    // Draws the occlusion of what the depth holds, a single-sample depth texture drawn through the camera, in passes
    // of its own, so outside any render pass. Returns it, the depth's size, from 0 where all is hidden to 1 where
    // nothing is, in red; null when it cannot. It stays valid until the next call.
    const Texture* Render(RenderContext& context, const Texture& depth, const Camera3D& camera,
                          const AmbientOcclusionSettings& settings);

private:
    AmbientOcclusion() = default;

    GraphicsDevice* m_device = nullptr;
    std::unique_ptr<FullscreenPass> m_occlusionPass;
    std::unique_ptr<FullscreenPass> m_blurPass;
    std::unique_ptr<Sampler> m_sampler;
    // The occlusion, and the same blurred along one axis, which the other axis blurs back into it.
    std::unique_ptr<Texture> m_occlusion;
    std::unique_ptr<Texture> m_blurred;
};

} // namespace Spot
