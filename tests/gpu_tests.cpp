#include "quad.vert.h"
#include "triangle.frag.h"
#include "triangle.vert.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace {

// Runs OnRender() for a few frames, giving up after a bounded number of updates in case no frame can be drawn.
class RenderingApplication final : public Spot::Application {
public:
    explicit RenderingApplication(std::function<void(Spot::Application&, Spot::RenderContext&)> render)
        : Application({.window = {.title = "Spot GPU Tests", .width = 320, .height = 240}}),
          m_render(std::move(render)) {}

    int GetRenderedFrames() const { return m_renderedFrames; }

protected:
    void OnUpdate(float) override {
        if (++m_updates > 200) {
            Quit();
        }
    }

    void OnRender(Spot::RenderContext& context) override {
        m_render(*this, context);
        if (++m_renderedFrames == 3) {
            Quit();
        }
    }

private:
    std::function<void(Spot::Application&, Spot::RenderContext&)> m_render;
    int m_updates = 0;
    int m_renderedFrames = 0;
};

void RequireGpu() {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No GPU backend is available on this machine");
    }
}

} // namespace

TEST_CASE("Application renders frames sized to the window in pixels", "[gpu]") {
    RequireGpu();
    std::string backend;
    int width = 0;
    int pixelWidth = 0;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        backend = app.GetGraphicsDevice().GetBackendName();
        width = context.GetWidth();
        pixelWidth = app.GetWindow().GetPixelWidth();
        auto pass = context.BeginRenderPass({.clearColor = Spot::Color::Blue});
        CHECK(pass.GetNativeHandle() != nullptr);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
    CHECK_FALSE(backend.empty());
    CHECK(width == pixelWidth);
}

TEST_CASE("Render passes can clear or keep the target, one after the other", "[gpu]") {
    RequireGpu();

    RenderingApplication application([](Spot::Application&, Spot::RenderContext& context) {
        auto clear = context.BeginRenderPass({.clearColor = Spot::Color::Black});
        clear.End();
        clear.End();

        auto keep = context.BeginRenderPass();
        CHECK(keep.GetNativeHandle() != nullptr);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
}

TEST_CASE("VSync can be turned off and back on", "[gpu]") {
    RequireGpu();
    bool enabledAgain = false;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        auto& device = app.GetGraphicsDevice();
        device.SetVSync(false);
        device.SetVSync(true);
        enabledAgain = device.IsVSyncEnabled();
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(enabledAgain);
}

TEST_CASE("Shaders and pipelines draw a triangle", "[gpu]") {
    RequireGpu();
    std::unique_ptr<Spot::Pipeline> pipeline;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        if (!pipeline) {
            auto& device = app.GetGraphicsDevice();
            const auto vertexShader = device.CreateShader(Shaders::TriangleVert);
            const auto fragmentShader = device.CreateShader(Shaders::TriangleFrag);
            REQUIRE(vertexShader != nullptr);
            REQUIRE(fragmentShader != nullptr);
            CHECK(vertexShader->GetStage() == Spot::ShaderStage::Vertex);

            pipeline = device.CreatePipeline({.vertexShader = vertexShader.get(),
                                              .fragmentShader = fragmentShader.get(),
                                              .blendMode = Spot::BlendMode::Alpha});
            REQUIRE(pipeline != nullptr);
        }

        auto pass = context.BeginRenderPass({.clearColor = Spot::Color::Black});
        pass.BindPipeline(*pipeline);
        pass.Draw(3);
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
    // Released after the device: the pipeline keeps the backend alive until then.
    pipeline.reset();
}

TEST_CASE("Shaders without code for the backend are rejected", "[gpu]") {
    RequireGpu();
    bool rejected = false;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        rejected = app.GetGraphicsDevice().CreateShader({.stage = Spot::ShaderStage::Vertex}) == nullptr;
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(rejected);
}

TEST_CASE("Vertex and index buffers draw an indexed quad with uniforms", "[gpu]") {
    RequireGpu();

    struct Vertex {
        Spot::Vector2 position;
        Spot::Color color;
    };
    const std::array<Vertex, 4> vertices{{
        {{-0.5f, -0.5f}, Spot::Color::Red},
        {{0.5f, -0.5f}, Spot::Color::Green},
        {{0.5f, 0.5f}, Spot::Color::Blue},
        {{-0.5f, 0.5f}, Spot::Color::White},
    }};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};

    std::unique_ptr<Spot::Buffer> vertexBuffer;
    std::unique_ptr<Spot::Buffer> indexBuffer;
    std::unique_ptr<Spot::Pipeline> pipeline;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext& context) {
        auto& device = app.GetGraphicsDevice();
        if (!pipeline) {
            vertexBuffer = device.CreateBuffer(Spot::BufferUsage::Vertex, vertices);
            indexBuffer = device.CreateBuffer(Spot::BufferUsage::Index, indices);
            REQUIRE(vertexBuffer != nullptr);
            REQUIRE(indexBuffer != nullptr);
            CHECK(vertexBuffer->GetSize() == sizeof(vertices));
            CHECK(indexBuffer->GetUsage() == Spot::BufferUsage::Index);

            const auto vertexShader = device.CreateShader(Shaders::QuadVert);
            const auto fragmentShader = device.CreateShader(Shaders::TriangleFrag);
            REQUIRE(vertexShader != nullptr);
            REQUIRE(fragmentShader != nullptr);
            pipeline = device.CreatePipeline({
                .vertexShader = vertexShader.get(),
                .fragmentShader = fragmentShader.get(),
                .vertexBuffers = {{
                    .stride = sizeof(Vertex),
                    .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                                   {1, Spot::VertexFormat::Float4, offsetof(Vertex, color)}},
                }},
            });
            REQUIRE(pipeline != nullptr);
        } else {
            // Partial updates during a frame, while earlier frames may still read the buffer.
            device.UploadToBuffer(*vertexBuffer, std::span(vertices).subspan(1, 2), sizeof(Vertex));
        }

        auto pass = context.BeginRenderPass({.clearColor = Spot::Color::Black});
        pass.BindPipeline(*pipeline);
        pass.BindVertexBuffer(0, *vertexBuffer);
        pass.BindIndexBuffer(*indexBuffer, Spot::IndexType::UInt16);
        pass.PushVertexUniforms(0, Spot::Matrix4::Scale({1.5f, 1.5f, 1.0f}));
        pass.DrawIndexed(static_cast<std::uint32_t>(indices.size()));
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.GetRenderedFrames() == 3);
}

TEST_CASE("Empty buffers are rejected", "[gpu]") {
    RequireGpu();
    bool rejected = false;

    RenderingApplication application([&](Spot::Application& app, Spot::RenderContext&) {
        rejected = app.GetGraphicsDevice().CreateBuffer({.usage = Spot::BufferUsage::Vertex, .size = 0}) == nullptr;
    });

    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(rejected);
}
