#include "viewport/viewport_renderer.h"

#include "core/editor_log.h"

#include "grid.frag.h"
#include "grid.vert.h"

namespace Spot::Editor {

namespace {

constexpr std::uint32_t PreferredSampleCount = 4;
// What shows where nothing is drawn, in scenes without a sky.
constexpr Color Background{0.035f, 0.038f, 0.045f, 1.0f};
constexpr float GridReach = 150.0f;

struct GridUniforms {
    Matrix4 inverseViewProjection;
    Matrix4 viewProjection;
    Vector4 camera;
};

// Scenes without any light would be black, which says nothing about what is in them, so the editor lights them as a
// studio would until they get lights of their own.
void DrawDefaultLights(Renderer3D& renderer) {
    renderer.DrawLight({.type = LightType::Ambient, .color = Color{0.35f, 0.37f, 0.42f, 1.0f}});
    renderer.DrawLight({.type = LightType::Directional,
                        .color = Color{1.0f, 0.97f, 0.92f, 1.0f},
                        .direction = {-0.45f, -1.0f, -0.35f},
                        .castsShadows = true});
}

} // namespace

bool ViewportTargets::Resize(GraphicsDevice& device, std::uint32_t width, std::uint32_t height,
                             std::uint32_t sampleCount) {
    if (width == m_width && height == m_height && sampleCount == m_sampleCount && m_color) {
        return true;
    }
    m_color = device.CreateTexture(
        {.width = width, .height = height, .format = ViewportRenderer::ColorFormat, .renderTarget = true});
    m_samples = sampleCount > 1 ? device.CreateTexture({.width = width,
                                                        .height = height,
                                                        .format = ViewportRenderer::ColorFormat,
                                                        .renderTarget = true,
                                                        .sampleCount = sampleCount})
                                : nullptr;
    m_depth = device.CreateTexture({.width = width,
                                    .height = height,
                                    .format = TextureFormat::Depth32Float,
                                    .renderTarget = true,
                                    .sampleCount = sampleCount});
    m_width = width;
    m_height = height;
    m_sampleCount = sampleCount;
    const bool created = m_color && m_depth && (sampleCount == 1 || m_samples);
    if (!created) {
        m_color.reset();
        m_samples.reset();
        m_depth.reset();
    }
    return created;
}

ViewportRenderer::ViewportRenderer(GraphicsDevice& device)
    : m_device(device),
      m_sampleCount(device.SupportsSampleCount(ColorFormat, PreferredSampleCount) ? PreferredSampleCount : 1) {
    m_renderer = std::make_unique<Renderer3D>(
        device, Renderer3DConfig{.targetFormat = ColorFormat, .sampleCount = m_sampleCount});

    const auto vertexShader = device.CreateShader(Shaders::GridVert);
    const auto fragmentShader = device.CreateShader(Shaders::GridFrag);
    if (vertexShader && fragmentShader) {
        m_grid = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .blendMode = BlendMode::Alpha,
            .cullMode = CullMode::None,
            .targetFormat = ColorFormat,
            // Hidden by what stands in front of it, without hiding anything drawn later.
            .depth = DepthState{.test = true, .write = false},
            .sampleCount = m_sampleCount,
        });
    }
    if (!m_grid) {
        GetEditorLogger().Warning("The viewport grid could not be created; views show no grid");
    }
}

ViewportRenderer::~ViewportRenderer() = default;

void ViewportRenderer::Render(RenderContext& context, const ViewportTargets& targets, Scene& scene,
                              const Camera3D& camera, const ViewportOptions& options) {
    if (!targets.GetColor()) {
        return;
    }
    const bool drawGrid = options.showGrid && m_grid;
    Texture* samples = targets.GetSamples();
    m_renderer->Begin(context,
                      {.target = samples ? samples : targets.GetColor(),
                       .clearColor = Background,
                       .depthTarget = targets.GetDepth(),
                       .clearDepth = 1.0f,
                       .resolveTarget = samples ? targets.GetColor() : nullptr,
                       .keepSamples = samples && drawGrid},
                      camera);
    if (!scene.FindComponent<Light>() && !scene.FindComponent<Sky>()) {
        DrawDefaultLights(*m_renderer);
    }
    m_renderer->DrawScene(scene);
    m_renderer->End();

    if (drawGrid) {
        DrawGrid(context, targets, camera);
    }
}

void ViewportRenderer::DrawGrid(RenderContext& context, const ViewportTargets& targets, const Camera3D& camera) {
    Texture* samples = targets.GetSamples();
    auto pass = context.BeginRenderPass({.target = samples ? samples : targets.GetColor(),
                                         .depthTarget = targets.GetDepth(),
                                         .resolveTarget = samples ? targets.GetColor() : nullptr});
    const Vector2 size{static_cast<float>(targets.GetWidth()), static_cast<float>(targets.GetHeight())};
    const Matrix4 viewProjection = camera.GetViewProjection(size);
    pass.BindPipeline(*m_grid);
    pass.PushFragmentUniforms(
        0, GridUniforms{.inverseViewProjection = Inverse(viewProjection),
                        .viewProjection = viewProjection,
                        .camera = {camera.position.x, camera.position.y, camera.position.z, GridReach}});
    pass.Draw(3);
}

} // namespace Spot::Editor
