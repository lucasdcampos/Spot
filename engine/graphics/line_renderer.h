#pragma once

#include "math/color.h"
#include "graphics/texture.h"
#include "math/matrix.h"
#include "math/vector.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Spot {

class Buffer;
class GraphicsDevice;
struct DebugLine;
class Pipeline;
class RenderPass;

struct LineRendererConfig {
    // The format of the textures it draws into; the window's swapchain when unset.
    std::optional<TextureFormat> targetFormat = std::nullopt;
    // The sample count of the targets of the passes it draws in; see TextureDesc::sampleCount.
    std::uint32_t sampleCount = 1;
    // Whether the passes it draws in have a Depth32Float depth target, which lines that test depth are hidden by.
    // Without one, every line is drawn over what is there.
    bool depthTarget = true;
};

// Draws lines in 3D, one pixel wide, inside a render pass the caller opens, built only on the public graphics API.
// Lines that test depth are drawn first, then the others over everything.
//
//     renderer.Begin(pass, camera.GetViewProjection(viewSize));
//     renderer.DrawLine({0, 0, 0}, {0, 1, 0}, Spot::Color::Green);
//     renderer.DrawDebugLines();
//     renderer.End();
class LineRenderer {
public:
    explicit LineRenderer(GraphicsDevice& device, const LineRendererConfig& config = {});
    ~LineRenderer();

    LineRenderer(const LineRenderer&) = delete;
    LineRenderer& operator=(const LineRenderer&) = delete;

    // Whether its GPU resources were created; when not, drawing does nothing and the reason has been logged.
    bool IsValid() const;

    void Begin(RenderPass& pass, const Matrix4& viewProjection);
    void End();

    void DrawLine(Vector3 from, Vector3 to, Color color, bool depthTest = true);
    void DrawLines(std::span<const DebugLine> lines);
    // Draws the lines queued with DebugDraw for this frame.
    void DrawDebugLines();

    // The lines the last End() drew.
    std::uint32_t GetLineCount() const { return m_lineCount; }

private:
    struct Vertex {
        Vector3 position;
        Color color;
    };

    bool EnsureCapacity(std::size_t vertexCount);

    GraphicsDevice& m_device;
    std::unique_ptr<Pipeline> m_tested;
    std::unique_ptr<Pipeline> m_overlay;
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::size_t m_capacity = 0;
    std::vector<Vertex> m_testedVertices;
    std::vector<Vertex> m_overlayVertices;
    RenderPass* m_pass = nullptr;
    Matrix4 m_viewProjection;
    std::uint32_t m_lineCount = 0;
};

} // namespace Spot
