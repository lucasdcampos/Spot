#include <spot.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A colored quad the sandbox draws at its entity's world position and rotation.
class Sprite final : public Spot::Component {
public:
    Sprite(Spot::Vector2 initialSize, Spot::Color initialColor) : size(initialSize), color(initialColor) {}

    Spot::Vector2 size;
    Spot::Color color;
};

// Turns its entity, and with it all its children.
class Spin final : public Spot::Component {
public:
    explicit Spin(float speed) : m_speed(speed) {}

protected:
    void OnUpdate(float deltaTime) override {
        auto& transform = GetEntity().GetTransform();
        transform.SetRotation2D(transform.GetRotation2D() + m_speed * deltaTime);
    }

private:
    float m_speed;
};

// A ring of quads that spins as a whole while each quad spins on its own.
class RingScene final : public Spot::Scene {
protected:
    void OnLoad() override {
        auto& ring = CreateEntity("Ring");
        ring.AddTag("Toggle");
        ring.AddComponent<Spin>(0.5f);

        constexpr int Quads = 12;
        for (int i = 0; i < Quads; ++i) {
            const float angle = static_cast<float>(i) * 2.0f * Spot::Pi / Quads;
            auto& quad = ring.CreateChild(std::format("Quad {}", i));
            quad.GetTransform().position = {std::cos(angle) * 250.0f, std::sin(angle) * 250.0f, 0.0f};
            quad.AddComponent<Sprite>(Spot::Vector2{40.0f, 40.0f},
                                      Spot::Color{0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle), 0.9f});
            quad.AddComponent<Spin>(-1.5f);
        }
    }
};

// A sun with a planet orbiting it and a moon orbiting the planet, each carried by its parent's rotation.
class SolarScene final : public Spot::Scene {
protected:
    void OnLoad() override {
        auto& sun = CreateEntity("Sun");
        sun.AddComponent<Sprite>(Spot::Vector2{90.0f, 90.0f}, Spot::Color{1.0f, 0.8f, 0.3f});
        sun.AddComponent<Spin>(0.4f);

        auto& planet = sun.CreateChild("Planet");
        planet.AddTag("Toggle");
        planet.GetTransform().position = {260.0f, 0.0f, 0.0f};
        planet.AddComponent<Sprite>(Spot::Vector2{40.0f, 40.0f}, Spot::Color{0.3f, 0.6f, 1.0f});
        planet.AddComponent<Spin>(2.0f);

        auto& moon = planet.CreateChild("Moon");
        moon.GetTransform().position = {60.0f, 0.0f, 0.0f};
        moon.AddComponent<Sprite>(Spot::Vector2{14.0f, 14.0f}, Spot::Color{0.8f, 0.8f, 0.8f});
    }
};

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

        // A short tone that fades out, made in code so the sandbox needs no sound file.
        constexpr std::uint32_t SampleRate = 48000;
        std::vector<float> samples(SampleRate / 8);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(SampleRate);
            samples[i] = 0.3f * std::sin(2.0f * Spot::Pi * 660.0f * t) * std::exp(-t * 30.0f);
        }
        m_blip = std::make_shared<Spot::Sound>(SampleRate, 1, std::move(samples));

        LoadScene<RingScene>();
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

        // Clicks play the tone, panned to where the mouse is and higher towards the top of the window.
        auto& audio = GetAudioDevice();
        if (Input::IsMouseButtonPressed(Spot::MouseButton::Left)) {
            const Spot::Vector2 mouse = Input::GetMousePosition();
            const Spot::Vector2 view = GetWindow().GetSize();
            audio.Play(m_blip, {.pan = mouse.x / view.x * 2.0f - 1.0f, .pitch = 2.0f - mouse.y / view.y});
        }
        if (Input::IsKeyPressed(Key::M)) {
            audio.SetMasterVolume(audio.GetMasterVolume() > 0.0f ? 0.0f : 1.0f);
        }

        // Tab switches scenes, and Space shows or hides the entities tagged "Toggle", children included.
        if (Input::IsKeyPressed(Key::Tab)) {
            m_solar = !m_solar;
            if (m_solar) {
                LoadScene<SolarScene>();
            } else {
                LoadScene<RingScene>();
            }
        }
        if (Input::IsKeyPressed(Key::Space) && GetScene()) {
            for (Spot::Entity* entity : GetScene()->FindEntitiesWithTag("Toggle")) {
                entity->SetActive(!entity->IsActiveSelf());
            }
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

        // The current scene's sprites, placed by their entity's world transform.
        if (Spot::Scene* scene = GetScene()) {
            scene->ForEachComponent<Sprite>([this](const Sprite& sprite) {
                const Spot::Entity& entity = sprite.GetEntity();
                const Spot::Vector3 position = entity.GetWorldPosition();
                const float rotation = Spot::ToEuler(entity.GetWorldRotation()).z;
                m_renderer->DrawRotatedQuad({position.x, position.y}, sprite.size, rotation, sprite.color);
            });
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
            m_renderer->DrawQuad({200.0f, -70.0f}, {400.0f, 140.0f}, Spot::Color{0.0f, 0.0f, 0.0f, 0.6f});
            m_renderer->DrawString(*m_font, "Spot Sandbox", {12.0f, -10.0f}, Spot::Color::White);
            m_renderer->DrawString(*m_font, m_status, {12.0f, -10.0f - line}, Spot::Color{0.7f, 0.9f, 0.7f});
            m_renderer->DrawString(*m_font, "WASD/arrows: move | wheel: zoom | Tab: scene | Space: toggle",
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
    std::shared_ptr<Spot::Sound> m_blip;
    Spot::Renderer2DStats m_worldStats;
    std::string m_status;
    Spot::Camera2D m_camera;
    bool m_solar = false;
    int m_frames = 0;
    float m_elapsed = 0.0f;
};

int main() {
    Sandbox sandbox;
    return sandbox.Run();
}
