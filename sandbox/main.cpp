#include <spot.h>

#include <format>

class Sandbox final : public Spot::Application {
public:
    Sandbox() : Application({.window = {.title = "Spot Sandbox"}, .logFile = "sandbox.log"}) {}

protected:
    void OnStart() override { Spot::Log::Info("Spot {}", Spot::GetVersionString()); }

    void OnEvent(const Spot::Event& event) override {
        if (const auto* pressed = event.GetIf<Spot::KeyPressedEvent>(); pressed && !pressed->repeat) {
            Spot::Log::Info("Key pressed: {}", Spot::GetKeyName(pressed->key));
        } else if (const auto* button = event.GetIf<Spot::MouseButtonPressedEvent>()) {
            Spot::Log::Info("Mouse button pressed: {} at ({}, {})", Spot::GetMouseButtonName(button->button),
                            Spot::Input::GetMouseX(), Spot::Input::GetMouseY());
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

private:
    int m_frames = 0;
    float m_elapsed = 0.0f;
};

int main() {
    Sandbox sandbox;
    return sandbox.Run();
}
