#pragma once

#include <memory>

namespace Spot {

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
};

enum class CullMode {
    None,
    Front,
    Back,
};

// The full GPU state for a draw. Pipelines are immutable: create one per combination that is needed, ahead of time,
// since creating them is slow. They draw into the window's swapchain.
struct PipelineDesc {
    const Shader* vertexShader = nullptr;
    const Shader* fragmentShader = nullptr;
    PrimitiveType primitiveType = PrimitiveType::TriangleList;
    BlendMode blendMode = BlendMode::None;
    // Front faces wind counterclockwise.
    CullMode cullMode = CullMode::None;
};

class Pipeline {
public:
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    // The backend's pipeline (an SDL_GPUGraphicsPipeline* with SDL_GPU), for code that needs the backend directly.
    void* GetNativeHandle() const;

private:
    friend class GraphicsDevice;

    struct Impl;

    explicit Pipeline(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
