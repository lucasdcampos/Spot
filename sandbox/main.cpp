#include <spot.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

class Sandbox final : public Spot::Application {
public:
    Sandbox() : Application({.window = {.title = "Spot Sandbox"}, .logFile = "sandbox.log", .maxFrameRate = 60}) {}

protected:
    void OnStart() override {
        Spot::Log::Info("Spot {}", Spot::GetVersionString());

        auto& device = GetGraphicsDevice();
        m_renderer = std::make_unique<Spot::Renderer2D>(device);
        if (const auto image = Spot::Image::Load(Spot::GetExecutableDirectory() / "assets" / "spot.png")) {
            m_spot = device.CreateTexture(*image);
        }

        const auto fonts = Spot::GetExecutableDirectory() / "assets" / "fonts";
        m_font = Spot::Font::Load(device, fonts / "FiraSans-Regular.ttf", 20.0f);
        m_largeFont = Spot::Font::Load(device, fonts / "FiraSans-Regular.ttf", 48.0f);
    }

    void OnEvent(const Spot::Event& event) override {
        if (const auto* pressed = event.GetIf<Spot::KeyPressedEvent>(); pressed && !pressed->repeat) {
            Spot::Log::Info("Key pressed: {}", Spot::GetKeyName(pressed->key));
        } else if (const auto* resized = event.GetIf<Spot::WindowResizeEvent>()) {
            Spot::Log::Info("Window resized to {}x{}", resized->width, resized->height);
        }
    }

    void OnUpdate(float deltaTime) override {
        namespace Input = Spot::Input;
        using Spot::Key;

        if (Input::IsKeyPressed(Key::Escape)) {
            Quit();
        }

        // Pans at a constant speed on screen, whatever the zoom.
        Spot::Vector2 direction;
        direction.x = static_cast<float>(Input::IsKeyDown(Key::D) || Input::IsKeyDown(Key::Right)) -
                      static_cast<float>(Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::Left));
        direction.y = static_cast<float>(Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::Up)) -
                      static_cast<float>(Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::Down));
        m_camera.position += Spot::Normalize(direction) * (400.0f / m_camera.zoom * deltaTime);

        if (const float scroll = Input::GetScrollDelta().y; scroll != 0.0f) {
            m_camera.zoom = std::clamp(m_camera.zoom * std::pow(1.1f, scroll), 0.25f, 8.0f);
        }

        ++m_frames;
        m_elapsed += deltaTime;
        if (m_elapsed >= 1.0f) {
            m_status = std::format("{} FPS, {} quads, {} draw calls", m_frames, m_worldStats.quadCount,
                                   m_worldStats.drawCalls);
            m_frames = 0;
            m_elapsed -= 1.0f;
        }
    }

    void OnRender(Spot::RenderContext& context) override {
        auto pass = context.BeginRenderPass({.clearColor = Spot::Color{0.08f, 0.08f, 0.1f}});
        if (!m_renderer) {
            return;
        }

        const Spot::Vector2 view = GetWindow().GetSize();
        const auto time = static_cast<float>(Spot::Time::GetTime());

        m_renderer->Begin(pass, m_camera, view);

        // A checkerboard of tiles shaded by their position.
        constexpr int Tiles = 16;
        constexpr float TileSize = 48.0f;
        for (int y = -Tiles; y < Tiles; ++y) {
            for (int x = -Tiles; x < Tiles; ++x) {
                const float shade = (x + y) % 2 == 0 ? 0.16f : 0.12f;
                const Spot::Color color{shade + 0.004f * static_cast<float>(x + Tiles), shade,
                                        shade + 0.004f * static_cast<float>(y + Tiles)};
                m_renderer->DrawQuad(
                    {(static_cast<float>(x) + 0.5f) * TileSize, (static_cast<float>(y) + 0.5f) * TileSize},
                    {TileSize - 2.0f, TileSize - 2.0f}, color);
            }
        }

        // Spinning colored quads in a ring.
        constexpr int RingQuads = 12;
        for (int i = 0; i < RingQuads; ++i) {
            const float angle = time * 0.5f + static_cast<float>(i) * 2.0f * Spot::Pi / RingQuads;
            const Spot::Vector2 position{std::cos(angle) * 250.0f, std::sin(angle) * 250.0f};
            const Spot::Color color{0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle), 0.9f};
            m_renderer->DrawRotatedQuad(position, {40.0f, 40.0f}, -angle * 2.0f, color);
        }

        // Textured spots orbiting the center, tinted and pulsing.
        if (m_spot) {
            for (int i = 0; i < 5; ++i) {
                const float angle = -time + static_cast<float>(i) * 2.0f * Spot::Pi / 5.0f;
                const float size = 80.0f + 16.0f * std::sin(time * 3.0f + static_cast<float>(i));
                m_renderer->DrawQuad({std::cos(angle) * 120.0f, std::sin(angle) * 120.0f}, {size, size}, *m_spot);
            }
            m_renderer->DrawRotatedQuad({0.0f, 0.0f}, {64.0f, 64.0f}, time, *m_spot, Spot::Color{0.6f, 0.9f, 1.0f});
        }

        // A translucent marker under the mouse cursor.
        const auto mouse = m_camera.ScreenToWorld(Spot::Input::GetMousePosition(), view);
        const bool held = Spot::Input::IsMouseButtonDown(Spot::MouseButton::Left);
        m_renderer->DrawRotatedQuad(mouse, {24.0f, 24.0f}, Spot::Pi / 4.0f,
                                    held ? Spot::Color{1.0f, 0.3f, 0.3f, 0.8f} : Spot::Color{1.0f, 1.0f, 1.0f, 0.5f});

        if (m_largeFont) {
            constexpr std::string_view Greeting = "Hello from world space";
            const auto size = m_largeFont->MeasureText(Greeting);
            m_renderer->DrawString(*m_largeFont, Greeting, {-size.x / 2.0f, 320.0f}, Spot::Color{1.0f, 0.85f, 0.5f});
        }

        m_renderer->End();
        m_worldStats = m_renderer->GetStats();

        // The HUD is drawn in a second round with a camera that puts world (0, 0) at the window's top-left corner,
        // so it stays fixed on screen while the world camera moves.
        if (m_font) {
            Spot::Camera2D screen;
            screen.position = {view.x / 2.0f, -view.y / 2.0f};
            const float line = m_font->GetLineHeight();

            m_renderer->Begin(pass, screen, view);
            m_renderer->DrawQuad({170.0f, -60.0f}, {340.0f, 120.0f}, Spot::Color{0.0f, 0.0f, 0.0f, 0.6f});
            m_renderer->DrawString(*m_font, "Spot Sandbox", {12.0f, -10.0f}, Spot::Color::White);
            m_renderer->DrawString(*m_font, m_status, {12.0f, -10.0f - line}, Spot::Color{0.7f, 0.9f, 0.7f});
            m_renderer->DrawString(*m_font, "WASD or arrows: move  |  wheel: zoom  |  Esc: quit",
                                   {12.0f, -10.0f - 2.0f * line}, Spot::Color{0.8f, 0.8f, 0.8f}, 0.8f);
            m_renderer->DrawString(*m_font, "Accents: caf\u00e9, fa\u00e7ade, na\u00efve, jalape\u00f1o",
                                   {12.0f, -10.0f - 2.8f * line}, Spot::Color{0.8f, 0.8f, 0.8f}, 0.8f);
            m_renderer->End();
        }
    }

private:
    std::unique_ptr<Spot::Renderer2D> m_renderer;
    std::unique_ptr<Spot::Texture> m_spot;
    std::unique_ptr<Spot::Font> m_font;
    std::unique_ptr<Spot::Font> m_largeFont;
    Spot::Renderer2DStats m_worldStats;
    std::string m_status;
    Spot::Camera2D m_camera;
    int m_frames = 0;
    float m_elapsed = 0.0f;
};

int main() {
    Sandbox sandbox;
    return sandbox.Run();
}
