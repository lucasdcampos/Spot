#pragma once

#include "graphics/camera_3d.h"
#include "graphics/color.h"
#include "graphics/texture.h"
#include "math/matrix.h"
#include "math/vector.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Spot {

class GraphicsDevice;
class Mesh;
class Model;
class Pipeline;
class RenderPass;
class Scene;
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
};

// A directional light, like the sun, plus ambient light that reaches every surface evenly.
struct Lighting {
    // The direction the light travels in, from the light toward the scene.
    Vector3 direction = {-0.4f, -1.0f, -0.6f};
    Color color = Color::White;
    // Keeps surfaces facing away from the light from going black.
    Color ambient = {0.25f, 0.25f, 0.3f};
};

struct Renderer3DStats {
    std::uint32_t drawCalls = 0;
    std::uint32_t triangleCount = 0;
};

// Draws lit, textured meshes with depth testing. Like Renderer2D, it is built only on the public graphics API and
// draws inside a render pass the caller opens, which must have a depth target:
//
//     auto pass = context.BeginRenderPass({.clearColor = Spot::Color::Black, .depthTarget = depth.get(),
//                                          .clearDepth = 1.0f});
//     renderer.Begin(pass, camera, viewSize);
//     renderer.DrawModel(*model, Spot::Matrix4::Translation({0, 0, -5}));
//     renderer.End();
//
// Surfaces are opaque. Meshes, models and textures must stay alive until End().
class Renderer3D {
public:
    explicit Renderer3D(GraphicsDevice& device, const Renderer3DConfig& config = {});
    ~Renderer3D();

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    // Whether its GPU resources were created; when not, drawing does nothing and the reason has been logged.
    bool IsValid() const;

    // The view size only sets the aspect ratio.
    void Begin(RenderPass& pass, const Camera3D& camera, Vector2 viewSize, const Lighting& lighting = {});
    void End();

    void DrawMesh(const Mesh& mesh, const Material& material, const Matrix4& transform = {});
    void DrawModel(const Model& model, const Matrix4& transform = {});
    // Draws every MeshRenderer of the scene that is enabled on an active entity, at its entity's world transform.
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
        Matrix4 transform;
    };

    void Flush();

    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Pipeline> m_doubleSidedPipeline;
    std::unique_ptr<Sampler> m_sampler;
    std::unique_ptr<Texture> m_whiteTexture;

    std::vector<DrawCommand> m_commands;
    RenderPass* m_pass = nullptr;
    Matrix4 m_viewProjection;
    Lighting m_lighting;
    Renderer3DStats m_stats;
};

} // namespace Spot
