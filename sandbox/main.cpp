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
    Sprite() = default;
    Sprite(Spot::Vector2 initialSize, Spot::Color initialColor) : size(initialSize), color(initialColor) {}

    Spot::Vector2 size = {32.0f, 32.0f};
    Spot::Color color = Spot::Color::White;

protected:
    void Serialize(Spot::Archive& archive) override {
        archive.Field("size", size);
        archive.Field("color", color);
    }
};

// Turns its entity, and with it all its children, around an axis: Z by default, as 2D needs.
class Spin final : public Spot::Component {
public:
    Spin() = default;
    explicit Spin(float speed, Spot::Vector3 axis = {0.0f, 0.0f, 1.0f}) : m_speed(speed), m_axis(axis) {}

protected:
    void OnUpdate(float deltaTime) override {
        GetEntity().GetTransform().Rotate(Spot::Quaternion::FromAxisAngle(m_axis, m_speed * deltaTime));
    }

    void Serialize(Spot::Archive& archive) override {
        archive.Field("speed", m_speed);
        archive.Field("axis", m_axis);
    }

private:
    float m_speed = 1.0f;
    Spot::Vector3 m_axis = {0.0f, 0.0f, 1.0f};
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
            quad.GetTransform().SetPosition({std::cos(angle) * 250.0f, std::sin(angle) * 250.0f, 0.0f});
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
        planet.GetTransform().SetPosition({260.0f, 0.0f, 0.0f});
        planet.AddComponent<Sprite>(Spot::Vector2{40.0f, 40.0f}, Spot::Color{0.3f, 0.6f, 1.0f});
        planet.AddComponent<Spin>(2.0f);

        auto& moon = planet.CreateChild("Moon");
        moon.GetTransform().SetPosition({60.0f, 0.0f, 0.0f});
        moon.AddComponent<Sprite>(Spot::Vector2{14.0f, 14.0f}, Spot::Color{0.8f, 0.8f, 0.8f});
    }
};

// The 3D view: a house loaded from glTF, a tumbling textured cube and spheres circling it on the ground, each shown
// by a MeshRenderer.
class ShowroomScene final : public Spot::Scene {
protected:
    void OnLoad() override {
        auto& device = GetApplication().GetGraphicsDevice();
        const auto assets = Spot::GetExecutableDirectory() / "assets";

        const std::shared_ptr<const Spot::Mesh> ground = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(16.0f));
        CreateEntity("Ground").AddComponent<Spot::MeshRenderer>(
            ground, Spot::Material{.baseColor = Spot::Color{0.35f, 0.55f, 0.3f}});

        // Each part of the model becomes a child, so the house moves as one entity.
        if (const auto model = Spot::Model::Load(device, assets / "models" / "house.glb")) {
            auto& house = CreateEntity("House");
            house.GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, 0.6f));
            for (const auto& part : model->GetParts()) {
                house.CreateChild(part.name).AddComponent<Spot::MeshRenderer>(part.mesh.GetShared(), *part.material);
            }
        }

        Spot::Material spotMaterial;
        if (const auto image = Spot::Image::Load(assets / "spot.png")) {
            // Mipmaps keep the texture smooth when seen from afar or at a grazing angle.
            spotMaterial.baseColorTexture = device.CreateTexture(*image, true);
        }
        auto& cube = CreateEntity("Cube");
        cube.GetTransform().SetPosition({0.0f, 2.9f, 0.0f});
        cube.GetTransform().SetScale({0.6f, 0.6f, 0.6f});
        cube.AddComponent<Spot::MeshRenderer>(Spot::Mesh::Create(device, Spot::MeshData::CreateCube()), spotMaterial);
        cube.AddComponent<Spin>(1.2f, Spot::Vector3{1.0f, 1.0f, 0.0f});

        const std::shared_ptr<const Spot::Mesh> sphere = Spot::Mesh::Create(device, Spot::MeshData::CreateSphere());
        auto& orbit = CreateEntity("Orbit");
        orbit.AddTag("Toggle");
        orbit.AddComponent<Spin>(0.5f, Spot::Vector3{0.0f, 1.0f, 0.0f});
        constexpr int Spheres = 6;
        for (int i = 0; i < Spheres; ++i) {
            const float angle = static_cast<float>(i) * 2.0f * Spot::Pi / Spheres;
            auto& ball = orbit.CreateChild(std::format("Sphere {}", i));
            ball.GetTransform().SetPosition({std::cos(angle) * 2.5f, 0.4f, std::sin(angle) * 2.5f});
            ball.GetTransform().SetScale({0.6f, 0.6f, 0.6f});
            ball.AddComponent<Spot::MeshRenderer>(
                sphere, Spot::Material{.baseColor = Spot::Color{0.5f + 0.5f * std::cos(angle * 2.0f),
                                                                0.5f + 0.5f * std::sin(angle), 0.9f}});
        }
    }
};

class Sandbox final : public Spot::Application {
public:
    Sandbox() : Application({.window = {.title = "Spot Sandbox"}, .logFile = "sandbox.log", .maxFrameRate = 60}) {
        Spot::RegisterComponent<Sprite>("Sprite");
        Spot::RegisterComponent<Spin>("Spin");
    }

protected:
    void OnStart() override {
        Spot::Log::Info("Spot {}", Spot::GetVersionString());

        auto& device = GetGraphicsDevice();
        m_renderer = std::make_unique<Spot::Renderer2D>(device);
        if (const auto image = Spot::Image::Load(Spot::GetExecutableDirectory() / "assets" / "spot.png")) {
            m_spot = device.CreateTexture(*image);
        }
        m_renderer3D = std::make_unique<Spot::Renderer3D>(device);

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

        // A satellite in the persistent scene, so it keeps orbiting whichever scene is loaded.
        auto& orbit = GetPersistentScene().CreateEntity("Satellite Orbit");
        orbit.AddComponent<Spin>(-0.8f);
        auto& satellite = orbit.CreateChild("Satellite");
        satellite.GetTransform().SetPosition({360.0f, 0.0f, 0.0f});
        satellite.AddComponent<Sprite>(Spot::Vector2{20.0f, 20.0f}, Spot::Color{1.0f, 0.4f, 0.6f});
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

        // F1 switches between the 2D scenes and the 3D one, Tab between the 2D scenes, and Space shows or hides the
        // entities tagged "Toggle", children included.
        if (Input::IsKeyPressed(Key::F1)) {
            m_show3D = !m_show3D;
            Load2DOr3DScene();
        }
        if (Input::IsKeyPressed(Key::Tab) && !m_show3D) {
            m_solar = !m_solar;
            Load2DOr3DScene();
        }
        if (Input::IsKeyPressed(Key::Space) && GetScene()) {
            for (Spot::Entity* entity : GetScene()->FindEntitiesWithTag("Toggle")) {
                entity->SetActive(!entity->IsActiveSelf());
            }
        }

        // F5 saves the main scene, in its current state, and F9 replaces it with the saved one.
        const auto savePath = Spot::GetExecutableDirectory() / "scene.json";
        if (Input::IsKeyPressed(Key::F5) && GetScene() && GetScene()->SaveToFile(savePath)) {
            Spot::Log::Info("Scene saved to {}", Spot::PathToUtf8(savePath));
        }
        if (Input::IsKeyPressed(Key::F9)) {
            auto loaded = std::make_unique<Spot::Scene>();
            if (loaded->LoadFromFile(savePath)) {
                LoadScene(std::move(loaded));
                Spot::Log::Info("Scene loaded from {}", Spot::PathToUtf8(savePath));
            }
        }

        Spot::Vector2 direction;
        direction.x = static_cast<float>(Input::IsKeyDown(Key::D) || Input::IsKeyDown(Key::Right)) -
                      static_cast<float>(Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::Left));
        direction.y = static_cast<float>(Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::Up)) -
                      static_cast<float>(Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::Down));
        const float scroll = Input::GetScrollDelta().y;

        if (m_show3D) {
            // The keys and a right-button drag orbit around the house, and the wheel moves closer or farther.
            Spot::Vector2 orbit = direction * (1.5f * deltaTime);
            if (Input::IsMouseButtonDown(Spot::MouseButton::Right)) {
                const Spot::Vector2 drag = Input::GetMouseDelta();
                orbit += Spot::Vector2{-drag.x, drag.y} * 0.005f;
            }
            m_orbitYaw += orbit.x;
            m_orbitPitch = std::clamp(m_orbitPitch + orbit.y, 0.05f, 1.4f);
            m_orbitDistance = std::clamp(m_orbitDistance * std::pow(0.9f, scroll), 2.0f, 30.0f);
        } else {
            // Pans at a constant speed on screen, whatever the zoom.
            m_camera.position += Spot::Normalize(direction) * (400.0f / m_camera.zoom * deltaTime);
            if (scroll != 0.0f) {
                m_camera.zoom = std::clamp(m_camera.zoom * std::pow(1.1f, scroll), 0.25f, 8.0f);
            }
        }

        ++m_frames;
        m_elapsed += deltaTime;
        if (m_elapsed >= 1.0f) {
            m_status = m_show3D ? std::format("{} FPS, {} triangles, {} draw calls", m_frames, m_stats3D.triangleCount,
                                              m_stats3D.drawCalls)
                                : std::format("{} FPS, {} quads, {} draw calls", m_frames, m_worldStats.quadCount,
                                              m_worldStats.drawCalls);
            m_frames = 0;
            m_elapsed -= 1.0f;
        }
    }

    void OnRender(Spot::RenderContext& context) override {
        const Spot::Vector2 view = GetWindow().GetSize();
        if (m_show3D) {
            Render3D(context, view);
        } else {
            auto pass = context.BeginRenderPass({.clearColor = Spot::Color{0.08f, 0.08f, 0.1f}});
            Render2D(pass, view);
        }

        // The HUD has its own pass, without the depth target of the 3D view, which Renderer2D does not draw with.
        auto hud = context.BeginRenderPass();
        RenderHud(hud, view);
    }

private:
    void Load2DOr3DScene() {
        if (m_show3D) {
            LoadScene<ShowroomScene>();
        } else if (m_solar) {
            LoadScene<SolarScene>();
        } else {
            LoadScene<RingScene>();
        }
    }

    void Render2D(Spot::RenderPass& pass, Spot::Vector2 view) {
        if (!m_renderer) {
            return;
        }
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

        // The sprites of every scene, placed by their entity's world transform.
        auto scenes = GetScenes();
        scenes.insert(scenes.begin(), &GetPersistentScene());
        for (Spot::Scene* scene : scenes) {
            scene->ForEachComponent<Sprite>([this](const Sprite& sprite) {
                const Spot::Transform& transform = sprite.GetEntity().GetTransform();
                const Spot::Vector3 position = transform.GetWorldPosition();
                m_renderer->DrawRotatedQuad({position.x, position.y}, sprite.size, transform.GetWorldRotation2D(),
                                            sprite.color);
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
    }

    void Render3D(Spot::RenderContext& context, Spot::Vector2 view) {
        // The depth target must match the frame, whose size changes with the window.
        const auto width = static_cast<std::uint32_t>(context.GetWidth());
        const auto height = static_cast<std::uint32_t>(context.GetHeight());
        if (!m_depth || m_depth->GetWidth() != width || m_depth->GetHeight() != height) {
            m_depth = GetGraphicsDevice().CreateTexture(
                {.width = width, .height = height, .format = Spot::TextureFormat::Depth32Float, .renderTarget = true});
        }

        auto pass = context.BeginRenderPass(
            {.clearColor = Spot::Color{0.5f, 0.68f, 0.85f}, .depthTarget = m_depth.get(), .clearDepth = 1.0f});
        if (!m_depth || !m_renderer3D) {
            return;
        }

        const Spot::Vector3 target{0.0f, 0.8f, 0.0f};
        Spot::Camera3D camera;
        camera.position = target + Spot::Vector3{std::sin(m_orbitYaw) * std::cos(m_orbitPitch), std::sin(m_orbitPitch),
                                                 std::cos(m_orbitYaw) * std::cos(m_orbitPitch)} *
                                       m_orbitDistance;
        camera.LookAt(target);

        m_renderer3D->Begin(pass, camera, view);
        m_renderer3D->DrawScene(GetPersistentScene());
        for (Spot::Scene* scene : GetScenes()) {
            m_renderer3D->DrawScene(*scene);
        }
        m_renderer3D->End();
        m_stats3D = m_renderer3D->GetStats();
    }

    // The HUD uses a camera that puts world (0, 0) at the window's top-left corner, so it stays fixed on screen while
    // the world camera moves.
    void RenderHud(Spot::RenderPass& pass, Spot::Vector2 view) {
        if (!m_renderer || !m_font) {
            return;
        }
        Spot::Camera2D screen;
        screen.position = {view.x / 2.0f, -view.y / 2.0f};
        const float line = m_font->GetLineHeight();
        const std::string_view controls =
            m_show3D ? "F1: 2D | WASD or right drag: orbit | wheel: zoom | Space: toggle"
                     : "F1: 3D | WASD: move | wheel: zoom | Tab: scene | Space: toggle | F5/F9: save/load";

        m_renderer->Begin(pass, screen, view);
        m_renderer->DrawQuad({250.0f, -70.0f}, {500.0f, 140.0f}, Spot::Color{0.0f, 0.0f, 0.0f, 0.6f});
        m_renderer->DrawString(*m_font, "Spot Sandbox", {12.0f, -10.0f}, Spot::Color::White);
        m_renderer->DrawString(*m_font, m_status, {12.0f, -10.0f - line}, Spot::Color{0.7f, 0.9f, 0.7f});
        m_renderer->DrawString(*m_font, controls, {12.0f, -10.0f - 2.0f * line}, Spot::Color{0.8f, 0.8f, 0.8f}, 0.8f);
        m_renderer->DrawString(*m_font, "Accents: caf\u00e9, fa\u00e7ade, na\u00efve, jalape\u00f1o",
                               {12.0f, -10.0f - 2.8f * line}, Spot::Color{0.8f, 0.8f, 0.8f}, 0.8f);
        m_renderer->End();
    }

    std::unique_ptr<Spot::Renderer2D> m_renderer;
    std::unique_ptr<Spot::Texture> m_spot;
    std::unique_ptr<Spot::Font> m_font;
    std::unique_ptr<Spot::Font> m_largeFont;
    std::shared_ptr<Spot::Sound> m_blip;
    Spot::Renderer2DStats m_worldStats;
    std::string m_status;
    Spot::Camera2D m_camera;

    std::unique_ptr<Spot::Renderer3D> m_renderer3D;
    std::unique_ptr<Spot::Texture> m_depth;
    Spot::Renderer3DStats m_stats3D;
    float m_orbitYaw = 0.6f;
    float m_orbitPitch = 0.45f;
    float m_orbitDistance = 5.0f;
    bool m_show3D = false;

    bool m_solar = false;
    int m_frames = 0;
    float m_elapsed = 0.0f;
};

int main() {
    Sandbox sandbox;
    return sandbox.Run();
}
