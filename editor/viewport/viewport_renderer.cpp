#include "viewport/viewport_renderer.h"

#include "core/editor_log.h"

#include "fullscreen.vert.h"
#include "grid.frag.h"
#include "outline.frag.h"
#include "silhouette.frag.h"
#include "silhouette.vert.h"

#include <algorithm>

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
    Vector4 options;
};

struct OutlineUniforms {
    Color color;
    Vector2 texelSize;
    float width = 0.0f;
    float padding = 0.0f;
};

// The masks and the picking target hold plain values, not colors.
constexpr TextureFormat ValueFormat = TextureFormat::Rgba8;

// Picking draws each mesh in a color that is its index plus one, so 0 is left for nothing.
Color EncodeIndex(std::size_t index) {
    const auto value = static_cast<std::uint32_t>(index + 1);
    const auto byte = [&](int shift) { return static_cast<float>((value >> shift) & 0xFFu) / 255.0f; };
    return {byte(0), byte(8), byte(16), 1.0f};
}

// Draws the meshes in the color last pushed, or each in the color of its index when picking.
void DrawSilhouettes(RenderPass& pass, const Matrix4& viewProjection, std::span<const MeshInstance> meshes,
                     bool picking) {
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        const MeshInstance& instance = meshes[i];
        if (!instance.mesh) {
            continue;
        }
        if (picking) {
            pass.PushFragmentUniforms(0, EncodeIndex(i));
        }
        pass.PushVertexUniforms(0, viewProjection * instance.world);
        pass.BindVertexBuffer(0, instance.mesh->GetVertexBuffer());
        pass.BindIndexBuffer(instance.mesh->GetIndexBuffer(), IndexType::UInt32);
        pass.DrawIndexed(instance.mesh->GetIndexCount());
    }
}

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
    m_mask = device.CreateTexture({.width = width, .height = height, .format = ValueFormat, .renderTarget = true});
    m_width = width;
    m_height = height;
    m_sampleCount = sampleCount;
    const bool created = m_color && m_depth && m_mask && (sampleCount == 1 || m_samples);
    if (!created) {
        m_color.reset();
        m_samples.reset();
        m_depth.reset();
        m_mask.reset();
    }
    return created;
}

ViewportRenderer::ViewportRenderer(GraphicsDevice& device)
    : m_device(device),
      m_sampleCount(device.SupportsSampleCount(ColorFormat, PreferredSampleCount) ? PreferredSampleCount : 1) {
    m_renderer = std::make_unique<Renderer3D>(
        device, Renderer3DConfig{.targetFormat = ColorFormat, .sampleCount = m_sampleCount});
    m_sprites = std::make_unique<Renderer2D>(
        device, Renderer2DConfig{.targetFormat = ColorFormat, .sampleCount = m_sampleCount});

    const auto fullscreenShader = device.CreateShader(Shaders::FullscreenVert);
    const auto gridShader = device.CreateShader(Shaders::GridFrag);
    if (fullscreenShader && gridShader) {
        m_grid = device.CreatePipeline({
            .vertexShader = fullscreenShader.get(),
            .fragmentShader = gridShader.get(),
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

    const auto silhouetteVertex = device.CreateShader(Shaders::SilhouetteVert);
    const auto silhouetteFragment = device.CreateShader(Shaders::SilhouetteFrag);
    const auto outlineShader = device.CreateShader(Shaders::OutlineFrag);
    if (silhouetteVertex && silhouetteFragment) {
        // Both sides of every triangle, so open meshes, like planes, can be picked and outlined from behind.
        const PipelineDesc silhouette{
            .vertexShader = silhouetteVertex.get(),
            .fragmentShader = silhouetteFragment.get(),
            .vertexBuffers = {Mesh::GetVertexLayout()},
            .targetFormat = ValueFormat,
        };
        m_mask = device.CreatePipeline(silhouette);
        PipelineDesc pick = silhouette;
        pick.depth = DepthState{};
        m_pick = device.CreatePipeline(pick);
    }
    if (fullscreenShader && outlineShader) {
        m_outline = device.CreatePipeline({
            .vertexShader = fullscreenShader.get(),
            .fragmentShader = outlineShader.get(),
            .blendMode = BlendMode::Alpha,
            .targetFormat = ColorFormat,
        });
    }
    m_maskSampler = device.CreateSampler({.filter = TextureFilter::Nearest});
    if (!m_mask || !m_outline || !m_maskSampler) {
        GetEditorLogger().Warning("The selection outline could not be created; views show no outline");
    }
    if (!m_pick) {
        GetEditorLogger().Warning("Picking could not be created; clicking views does not select");
    }
}

ViewportRenderer::~ViewportRenderer() = default;

void ViewportRenderer::Render(RenderContext& context, const ViewportTargets& targets, Scene& scene,
                              const Camera3D& camera, const ViewportOptions& options) {
    if (!targets.GetColor()) {
        return;
    }
    const bool drawGrid = options.showGrid && m_grid;
    const bool drawSprites = m_sprites->IsValid() && scene.FindComponent<SpriteRenderer>();
    Texture* samples = targets.GetSamples();
    m_renderer->Begin(context,
                      {.target = samples ? samples : targets.GetColor(),
                       .clearColor = options.background.value_or(Background),
                       .depthTarget = targets.GetDepth(),
                       .clearDepth = 1.0f,
                       .resolveTarget = samples ? targets.GetColor() : nullptr,
                       .keepSamples = samples && (drawGrid || drawSprites)},
                      camera);
    if (options.defaultLights && !scene.FindComponent<Light>() && !scene.FindComponent<Sky>()) {
        DrawDefaultLights(*m_renderer);
    }
    m_renderer->DrawScene(scene);
    m_renderer->End();

    // Sprites go over the meshes, as 2D games draw them, without a depth target.
    if (drawSprites) {
        auto pass = context.BeginRenderPass({.target = samples ? samples : targets.GetColor(),
                                             .resolveTarget = samples ? targets.GetColor() : nullptr,
                                             .keepSamples = samples && drawGrid});
        const Vector2 size{static_cast<float>(targets.GetWidth()), static_cast<float>(targets.GetHeight())};
        m_sprites->Begin(pass, camera.GetViewProjection(size));
        m_sprites->DrawScene(scene);
        m_sprites->End();
    }

    if (drawGrid) {
        DrawGrid(context, targets, camera, options.grid2D);
    }
}

void ViewportRenderer::DrawOutline(RenderContext& context, const ViewportTargets& targets, const Camera3D& camera,
                                   std::span<const MeshInstance> meshes, Color color, float width) {
    Texture* mask = targets.GetSelectionMask();
    if (meshes.empty() || !mask || !m_mask || !m_outline || !m_maskSampler) {
        return;
    }
    const Vector2 size{static_cast<float>(targets.GetWidth()), static_cast<float>(targets.GetHeight())};
    {
        auto pass = context.BeginRenderPass({.target = mask, .clearColor = Color{0.0f, 0.0f, 0.0f, 0.0f}});
        pass.BindPipeline(*m_mask);
        pass.PushFragmentUniforms(0, Color{1.0f, 1.0f, 1.0f, 1.0f});
        DrawSilhouettes(pass, camera.GetViewProjection(size), meshes, false);
    }
    auto pass = context.BeginRenderPass({.target = targets.GetColor()});
    pass.BindPipeline(*m_outline);
    pass.BindFragmentTexture(0, *mask, *m_maskSampler);
    pass.PushFragmentUniforms(0, OutlineUniforms{.color = color,
                                                 .texelSize = {1.0f / size.x, 1.0f / size.y},
                                                 .width = std::clamp(width, 0.0f, 3.0f)});
    pass.Draw(3);
}

void ViewportRenderer::RenderPick(RenderContext& context, const Camera3D& camera, Vector2 viewSize, Vector2 pixel,
                                  std::span<const MeshInstance> meshes) {
    if (!m_pick || viewSize.x <= 0.0f || viewSize.y <= 0.0f) {
        return;
    }
    if (!m_pickColor) {
        m_pickColor = m_device.CreateTexture({.width = 1, .height = 1, .format = ValueFormat, .renderTarget = true});
        m_pickDepth = m_device.CreateTexture(
            {.width = 1, .height = 1, .format = TextureFormat::Depth32Float, .renderTarget = true});
        if (!m_pickColor || !m_pickDepth) {
            m_pickColor.reset();
            m_pickDepth.reset();
            return;
        }
    }
    // Zooms into the pixel, so it fills the whole one-pixel target.
    const float x = (pixel.x + 0.5f) / viewSize.x * 2.0f - 1.0f;
    const float y = 1.0f - (pixel.y + 0.5f) / viewSize.y * 2.0f;
    const Matrix4 zoom = Matrix4::Scale({viewSize.x, viewSize.y, 1.0f}) * Matrix4::Translation({-x, -y, 0.0f});
    auto pass = context.BeginRenderPass({.target = m_pickColor.get(),
                                         .clearColor = Color{0.0f, 0.0f, 0.0f, 0.0f},
                                         .depthTarget = m_pickDepth.get(),
                                         .clearDepth = 1.0f});
    pass.BindPipeline(*m_pick);
    DrawSilhouettes(pass, zoom * camera.GetViewProjection(viewSize), meshes, true);
    m_pickDrawn = true;
}

std::optional<std::size_t> ViewportRenderer::ReadPick() {
    if (!m_pickColor || !std::exchange(m_pickDrawn, false)) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> pixels = m_device.DownloadTexture(*m_pickColor);
    if (pixels.size() < 4) {
        return std::nullopt;
    }
    const std::uint32_t value = pixels[0] | (pixels[1] << 8u) | (pixels[2] << 16u);
    return value == 0 ? std::nullopt : std::optional<std::size_t>(value - 1);
}

void ViewportRenderer::DrawGrid(RenderContext& context, const ViewportTargets& targets, const Camera3D& camera,
                                bool plane2D) {
    Texture* samples = targets.GetSamples();
    auto pass = context.BeginRenderPass({.target = samples ? samples : targets.GetColor(),
                                         .depthTarget = targets.GetDepth(),
                                         .resolveTarget = samples ? targets.GetColor() : nullptr});
    const Vector2 size{static_cast<float>(targets.GetWidth()), static_cast<float>(targets.GetHeight())};
    const Matrix4 viewProjection = camera.GetViewProjection(size);
    pass.BindPipeline(*m_grid);
    // In 2D the grid reaches a few views across, however far the view is zoomed out.
    const float reach = plane2D ? camera.orthographicSize * 8.0f : GridReach;
    pass.PushFragmentUniforms(0,
                              GridUniforms{.inverseViewProjection = Inverse(viewProjection),
                                           .viewProjection = viewProjection,
                                           .camera = {camera.position.x, camera.position.y, camera.position.z, reach},
                                           .options = {plane2D ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}});
    pass.Draw(3);
}

} // namespace Spot::Editor
