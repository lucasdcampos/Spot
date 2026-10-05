#include <spot.h>

#include <cmath>
#include <format>

class Sandbox final : public Spot::Application {
public:
    Sandbox() : Application({.window = {.title = "Spot Sandbox"}, .logFile = "sandbox.log", .maxFrameRate = 60}) {}

protected:
    void OnStart() override { Spot::Log::Info("Spot {}", Spot::GetVersionString()); }

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
        // Slowly cycles the background color to show that frames are being drawn.
        const auto time = static_cast<float>(Spot::Time::GetTime());
        const Spot::Color color{0.5f + 0.5f * std::sin(time), 0.5f + 0.5f * std::sin(time + 2.0f),
                                0.5f + 0.5f * std::sin(time + 4.0f)};
        context.BeginRenderPass({.clearColor = color});
    }

private:
    int m_frames = 0;
    float m_elapsed = 0.0f;
};

int main() {
    Sandbox sandbox;
    return sandbox.Run();
}
