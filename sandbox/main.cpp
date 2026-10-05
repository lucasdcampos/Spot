#include "triangle.frag.h"
#include "triangle.vert.h"

#include <spot.h>

#include <format>
#include <memory>

class Sandbox final : public Spot::Application {
public:
    Sandbox() : Application({.window = {.title = "Spot Sandbox"}, .logFile = "sandbox.log", .maxFrameRate = 60}) {}

protected:
    void OnStart() override {
        Spot::Log::Info("Spot {}", Spot::GetVersionString());

        auto& device = GetGraphicsDevice();
        const auto vertexShader = device.CreateShader(Shaders::TriangleVert);
        const auto fragmentShader = device.CreateShader(Shaders::TriangleFrag);
        if (vertexShader && fragmentShader) {
            m_triangle =
                device.CreatePipeline({.vertexShader = vertexShader.get(), .fragmentShader = fragmentShader.get()});
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
        if (m_triangle) {
            pass.BindPipeline(*m_triangle);
            pass.Draw(3);
        }
    }

private:
    std::unique_ptr<Spot::Pipeline> m_triangle;
    int m_frames = 0;
    float m_elapsed = 0.0f;
};

int main() {
    Sandbox sandbox;
    return sandbox.Run();
}
