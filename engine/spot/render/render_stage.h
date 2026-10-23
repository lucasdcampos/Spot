#pragma once

#include "spot/core/matrix.h"
#include "spot/core/vector.h"
#include "spot/graphics/render_context.h"
#include "spot/graphics/texture.h"
#include "spot/render/camera_3d.h"

#include <cstdint>
#include <functional>

namespace Spot {

// The points of drawing a frame where games and systems add passes of their own; see Scene::AddRenderHook().
enum class RenderStage : std::uint8_t {
    // Into the cleared HDR image, before the scene's surfaces. A sky draws over what is drawn here.
    BeforeOpaque,
    // After the scene's surfaces, with their depth: where the shapes queued with DebugDraw are drawn.
    AfterOpaque,
    // After what is drawn with transparency, the sprites for now.
    AfterTransparent,
    // Last into the HDR image, before bloom, tonemapping and FXAA.
    BeforePostProcessing,
    // Into the target, in its colors, after post-processing and before the interfaces.
    AfterPostProcessing,
    // Over everything, the interfaces included.
    Overlay,
};

class GraphicsDevice;

// What a render hook draws with: the device and the frame, where it is seen from, and the targets of its stage.
struct RenderStageContext {
    GraphicsDevice& device;
    RenderContext& context;
    RenderStage stage;
    Camera3D camera;
    Matrix4 viewProjection;
    Vector2 viewSize;
    // A pass into the stage's targets that keeps what they hold, depth included before post-processing. Pipelines
    // that do not test depth drop its depthTarget.
    RenderPassDesc pass;
    // What pipelines drawing in the stage's passes must match.
    TextureFormat colorFormat;
    std::uint32_t sampleCount;
    // The depth of the scene's surfaces; null after post-processing.
    Texture* depth;
    // The HDR image as drawn so far, single-sampled, for effects that read it while drawing into another target; null
    // after post-processing.
    const Texture* sceneColor;
};

using RenderHook = std::function<void(const RenderStageContext&)>;

enum class RenderHookId : std::uint32_t { Invalid = 0 };

} // namespace Spot
