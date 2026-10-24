// The engine as a library, with a loop of its own: no Application, no scenes, only the levels below them. A square
// follows the arrow keys; the game keeps its state as it likes.
#include <spot/graphics.h>
#include <spot/platform.h>
#include <spot/render.h>

#include <chrono>
#include <cstdlib>
#include <string_view>

int main(int argc, char** argv) {
    // The tests run the examples for a few frames: "--frames <count>".
    int frames = argc == 3 && std::string_view(argv[1]) == "--frames" ? std::atoi(argv[2]) : -1;

    auto window = Spot::Window::Create({.title = "Own loop", .width = 640, .height = 360});
    if (!window) {
        return EXIT_FAILURE;
    }
    auto device = Spot::GraphicsDevice::Create(*window);
    if (!device) {
        return EXIT_FAILURE;
    }
    Spot::Renderer2D renderer(*device);
    Spot::InputState input;
    Spot::Vector2 position = {};

    bool running = true;
    auto last = std::chrono::steady_clock::now();
    while (running && frames != 0) {
        const auto now = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(now - last).count();
        last = now;

        input.BeginFrame();
        Spot::Platform::PollEvents([&](const Spot::Event& event) {
            running = running && !event.Is<Spot::WindowCloseEvent>();
            input.OnEvent(event);
        });
        const Spot::Vector2 direction = {static_cast<float>(input.IsKeyDown(Spot::Key::Right)) -
                                             static_cast<float>(input.IsKeyDown(Spot::Key::Left)),
                                         static_cast<float>(input.IsKeyDown(Spot::Key::Up)) -
                                             static_cast<float>(input.IsKeyDown(Spot::Key::Down))};
        position += direction * 200.0f * deltaTime;

        // Null while nothing can be drawn, such as while the window is minimized.
        if (auto* context = device->BeginFrame()) {
            auto pass = context->BeginRenderPass({.clearColor = Spot::Color{0.1f, 0.1f, 0.15f, 1.0f}});
            renderer.Begin(pass, Spot::Camera2D{}, window->GetSize());
            renderer.DrawQuad(position, {50.0f, 50.0f}, Spot::Color::Green);
            renderer.End();
            pass.End();
            device->EndFrame();
        }
        if (frames > 0) {
            --frames;
        }
    }
    return EXIT_SUCCESS;
}
