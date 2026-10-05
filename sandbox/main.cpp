#include "color.frag.h"
#include "quad.vert.h"

#include <spot.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>

struct Vertex {
    Spot::Vector2 position;
    Spot::Color color;
};

constexpr std::array<Vertex, 4> QuadVertices{{
    {{-0.5f, -0.5f}, Spot::Color::Red},
    {{0.5f, -0.5f}, Spot::Color::Green},
    {{0.5f, 0.5f}, Spot::Color::Blue},
    {{-0.5f, 0.5f}, Spot::Color::White},
}};

constexpr std::array<std::uint16_t, 6> QuadIndices{0, 1, 2, 2, 3, 0};

class Sandbox final : public Spot::Application {
public:
    Sandbox() : Application({.window = {.title = "Spot Sandbox"}, .logFile = "sandbox.log", .maxFrameRate = 60}) {}

protected:
    void OnStart() override {
        Spot::Log::Info("Spot {}", Spot::GetVersionString());

        auto& device = GetGraphicsDevice();
        m_vertices = device.CreateBuffer(Spot::BufferUsage::Vertex, QuadVertices);
        m_indices = device.CreateBuffer(Spot::BufferUsage::Index, QuadIndices);

        const auto vertexShader = device.CreateShader(Shaders::QuadVert);
        const auto fragmentShader = device.CreateShader(Shaders::ColorFrag);
        if (vertexShader && fragmentShader) {
            m_pipeline = device.CreatePipeline({
                .vertexShader = vertexShader.get(),
                .fragmentShader = fragmentShader.get(),
                .vertexBuffers = {{
                    .stride = sizeof(Vertex),
                    .attributes = {{0, Spot::VertexFormat::Float2, offsetof(Vertex, position)},
                                   {1, Spot::VertexFormat::Float4, offsetof(Vertex, color)}},
                }},
            });
        }
    }

    void OnEvent(const Spot::Event& event) override {
        if (const auto* pressed = event.GetIf<Spot::KeyPressedEvent>(); pressed && !pressed->repeat) {
            Spot::Log::Info("Key pressed: {}", Spot::GetKeyName(pressed->key));
        } else if (const auto* button = event.GetIf<Spot::MouseButtonPressedEvent>()) {
            const auto position = Spot::Input::GetMousePosition();
            Spot::Log::Info("Mouse button pressed: {} at ({}, {})", Spot::GetMouseButtonName(button->button),
                            position.x, position.y);
        } else if (const auto* resized = event.GetIf<Spot::WindowResizeEvent>()) {
            Spot::Log::Info("Window resized to {}x{}", resized->width, resized->height);
        } else if (const auto* focus = event.GetIf<Spot::WindowFocusEvent>()) {
            Spot::Log::Info("Window {} focus", focus->focused ? "gained" : "lost");
        }
    }

    void OnUpdate(float deltaTime) override {
        if (Spot::Input::IsKeyPressed(Spot::Key::Escape)) {
            Quit();
        }

        ++m_frames;
        m_elapsed += deltaTime;
        if (m_elapsed >= 1.0f) {
            GetWindow().SetTitle(std::format("Spot Sandbox - {} FPS", m_frames));
            m_frames = 0;
            m_elapsed -= 1.0f;
        }
    }

    void OnRender(Spot::RenderContext& context) override {
        auto pass = context.BeginRenderPass({.clearColor = Spot::Color{0.1f, 0.1f, 0.12f}});
        if (!m_pipeline || !m_vertices || !m_indices) {
            return;
        }

        // Keeps the quad square whatever the window's aspect ratio, and spins it once every four seconds.
        const float aspect = static_cast<float>(context.GetWidth()) / static_cast<float>(context.GetHeight());
        const auto projection = Spot::Matrix4::Orthographic(-aspect, aspect, -1.0f, 1.0f, -1.0f, 1.0f);
        const auto angle = static_cast<float>(Spot::Time::GetTime()) * Spot::Pi / 2.0f;
        const auto rotation = Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, angle));

        pass.BindPipeline(*m_pipeline);
        pass.BindVertexBuffer(0, *m_vertices);
        pass.BindIndexBuffer(*m_indices, Spot::IndexType::UInt16);
        pass.PushVertexUniforms(0, projection * rotation);
        pass.DrawIndexed(static_cast<std::uint32_t>(QuadIndices.size()));
    }

private:
    std::unique_ptr<Spot::Buffer> m_vertices;
    std::unique_ptr<Spot::Buffer> m_indices;
    std::unique_ptr<Spot::Pipeline> m_pipeline;
    int m_frames = 0;
    float m_elapsed = 0.0f;
};

int main() {
    Sandbox sandbox;
    return sandbox.Run();
}
