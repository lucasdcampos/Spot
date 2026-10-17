#pragma once

#include "graphics/texture.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Spot {

namespace Detail {
struct GraphicsAccess;
} // namespace Detail

class Shader;

enum class PrimitiveType {
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip,
    PointList,
};

enum class BlendMode {
    // Replaces the target color.
    None,
    // Blends by the source alpha, for straight (non-premultiplied) alpha.
    Alpha,
    // Adds the source color weighted by its alpha, for glows and particles.
    Additive,
    // Adds the source color as it is and keeps what is behind by one minus the source alpha, for colors already
    // multiplied by their coverage: surfaces that let light through yet reflect at full strength, such as glass.
    Premultiplied,
};

enum class CullMode {
    None,
    Front,
    Back,
};

// How a pipeline uses the depth target of the passes it draws in.
struct DepthState {
    TextureFormat format = TextureFormat::Depth32Float;
    // Drops fragments whose depth fails the comparison with the depth already stored.
    bool test = true;
    bool write = true;
    CompareOp compare = CompareOp::Less;
    // Pushes the depth written away from the viewer by a constant, in units of the smallest step the depth format
    // can hold at that depth, plus a factor of how steeply the triangle slopes away. Shadow maps need it so surfaces
    // do not shadow themselves.
    float biasConstant = 0.0f;
    float biasSlope = 0.0f;
    // Clamps depths past the near and far planes to them instead of clipping what is there, as shadow maps of
    // directional lights need: what lies between the light and the slice of the scene they cover still casts its
    // shadow, flattened onto the near plane.
    bool clamp = false;
};

enum class VertexFormat {
    Float,
    Float2,
    Float3,
    Float4,
    UInt4,
    // Four bytes read as floats in [0, 1], a compact way to store colors.
    UByte4Normalized,
};

std::uint32_t GetVertexFormatSize(VertexFormat format);

// Attribute N feeds the vertex shader input at location N. In HLSL that is the N-th field of the input struct, and
// Direct3D 12 also requires its semantic to be TEXCOORD{N}.
struct VertexAttribute {
    std::uint32_t location = 0;
    VertexFormat format = VertexFormat::Float;
    // In bytes, from the start of the vertex.
    std::uint32_t offset = 0;
};

enum class VertexInputRate {
    Vertex,
    // Advances once per instance, for per-instance data such as transforms.
    Instance,
};

// The layout of the vertex buffer bound to one slot.
struct VertexBufferLayout {
    // In bytes, from one vertex to the next.
    std::uint32_t stride = 0;
    std::vector<VertexAttribute> attributes = {};
    VertexInputRate inputRate = VertexInputRate::Vertex;
};

// The full GPU state for a draw. Pipelines are immutable: create one per combination that is needed, ahead of time,
// since creating them is slow.
struct PipelineDesc {
    const Shader* vertexShader = nullptr;
    const Shader* fragmentShader = nullptr;
    // One layout per vertex buffer slot. Empty when the shader builds its vertices from SV_VertexID.
    std::vector<VertexBufferLayout> vertexBuffers = {};
    PrimitiveType primitiveType = PrimitiveType::TriangleList;
    BlendMode blendMode = BlendMode::None;
    // Front faces wind counterclockwise.
    CullMode cullMode = CullMode::None;
    // The format of the texture the pipeline draws into; the window's swapchain when unset.
    std::optional<TextureFormat> targetFormat = std::nullopt;
    // Draws only depth, in passes created with RenderPassDesc::depthOnly, and only in those; targetFormat and
    // blendMode are then ignored, and what the fragment shader returns is discarded.
    bool depthOnly = false;
    // Set for pipelines that draw in passes with a depth target, and only for those: the backends reject pipelines
    // whose targets differ from the pass's.
    std::optional<DepthState> depth = std::nullopt;
    // The sample count of the targets of the passes it draws in; see TextureDesc::sampleCount.
    std::uint32_t sampleCount = 1;
};

class Pipeline {
public:
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    // The backend's pipeline (an SDL_GPUGraphicsPipeline* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

    // Backend-internal: what a graphics backend derives its implementation from (see graphics/graphics_backend.h).
    struct Impl;

private:
    friend struct Detail::GraphicsAccess;

    explicit Pipeline(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
