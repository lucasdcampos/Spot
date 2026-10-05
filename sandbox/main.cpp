#include <spot.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>

namespace {

using Spot::Assets::Load;
using Spot::BuiltInAssets::CubeMesh;
using Spot::BuiltInAssets::SphereMesh;

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};
constexpr Spot::Vector3 SpawnPoint = {0.0f, 1.5f, 10.0f};

// Walks a capsule with WASD, Shift to run and Space to jump, and turns its view with the mouse. The body never turns,
// so it cannot tip over; only the view does.
class PlayerController final : public Spot::Component {
public:
    static constexpr float EyeHeight = 0.7f;

    Spot::Quaternion GetViewRotation() const {
        return Spot::Quaternion::FromAxisAngle(Up, m_yaw) *
               Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, m_pitch);
    }

    void Look(Spot::Vector2 mouseDelta) {
        constexpr float Sensitivity = 0.0025f;
        m_yaw -= mouseDelta.x * Sensitivity;
        m_pitch = std::clamp(m_pitch - mouseDelta.y * Sensitivity, -1.5f, 1.5f);
    }

    // A light that follows where the player looks.
    void SetFlashlight(Spot::Entity* flashlight) { m_flashlight = flashlight; }

protected:
    void OnUpdate(float /*deltaTime*/) override {
        // Read every frame, since a press can fall between two physics steps.
        m_jump = m_jump || Spot::Input::IsKeyPressed(Spot::Key::Space);
    }

    void OnLateUpdate(float /*deltaTime*/) override {
        if (m_flashlight) {
            m_flashlight->GetTransform().SetRotation(GetViewRotation());
        }
    }

    void OnFixedUpdate(float /*fixedDeltaTime*/) override {
        namespace Input = Spot::Input;
        using Spot::Key;

        auto* body = GetEntity().GetComponent<Spot::Rigidbody>();
        if (!body) {
            return;
        }

        const auto yaw = Spot::Quaternion::FromAxisAngle(Up, m_yaw);
        const float forward =
            static_cast<float>(Input::IsKeyDown(Key::W)) - static_cast<float>(Input::IsKeyDown(Key::S));
        const float right = static_cast<float>(Input::IsKeyDown(Key::D)) - static_cast<float>(Input::IsKeyDown(Key::A));
        const Spot::Vector3 direction = Spot::Normalize(yaw * Spot::Vector3{right, 0.0f, -forward});
        const float speed = Input::IsKeyDown(Key::LeftShift) ? 9.0f : 5.0f;

        // There are no ground queries yet, so the player counts as grounded while not moving up or down.
        Spot::Vector3 velocity = body->GetLinearVelocity();
        const bool grounded = std::abs(velocity.y) < 0.1f;
        velocity.x = direction.x * speed;
        velocity.z = direction.z * speed;
        if (m_jump && grounded) {
            velocity.y = 5.5f;
        }
        m_jump = false;

        body->SetLinearVelocity(velocity);
        body->SetAngularVelocity({});
        GetEntity().GetTransform().SetRotation({});
    }

private:
    Spot::Entity* m_flashlight = nullptr;
    float m_yaw = 0.0f;
    float m_pitch = -0.15f;
    bool m_jump = false;
};

// A walled floor with ramps, steps, a pyramid and a tower of crates, and balls to knock them over with, lit by colored
// lamps and a sun that crosses the sky in a two-minute day.
class PlaygroundScene final : public Spot::Scene {
public:
    PlayerController* GetPlayer() const { return m_player; }

    void ToggleFlashlight() {
        if (m_flashlight) {
            m_flashlight->SetEnabled(!m_flashlight->IsEnabled());
        }
    }

    // From 0 at midnight to 1 at the next one.
    float GetTimeOfDay() const { return m_timeOfDay; }

    // Throws a ball from the player's eyes, where they look.
    void Shoot() {
        if (!m_player) {
            return;
        }
        const auto view = m_player->GetViewRotation();
        const Spot::Vector3 forward = view * Spot::Vector3{0.0f, 0.0f, -1.0f};
        const Spot::Vector3 eye = m_player->GetEntity().GetTransform().GetWorldPosition() +
                                  Spot::Vector3{0.0f, PlayerController::EyeHeight, 0.0f};

        auto& ball = AddBall(eye + forward * 0.9f, 0.4f, "materials/pink.material");
        ball.GetComponent<Spot::Rigidbody>()->SetLinearVelocity(forward * 25.0f);
    }

protected:
    void OnLoad() override {
        m_sun = &CreateEntity("Sun").AddComponent<Spot::DirectionalLight>();
        auto& sky = CreateEntity("Sky");
        // Grassland beyond the walls.
        sky.AddComponent<Spot::Sky>().SetGroundColor(Spot::Color::FromSrgb(0.3f, 0.36f, 0.22f));
        UpdateSun();

        AddLamp({-10.0f, 3.5f, -8.0f}, {1.0f, 0.5f, 0.2f});
        AddLamp({0.0f, 2.5f, -3.5f}, {0.3f, 1.0f, 0.4f});
        AddLamp({8.0f, 4.0f, -6.0f}, {0.9f, 0.3f, 1.0f});
        AddLamp({10.0f, 2.0f, 4.0f}, {0.3f, 0.6f, 1.0f});

        AddBlock("Ground", {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f}, "materials/ground.material");
        for (const float side : {-1.0f, 1.0f}) {
            AddBlock("Wall", {side * 20.5f, 1.0f, 0.0f}, {1.0f, 2.0f, 42.0f}, "materials/cyan.material");
            AddBlock("Wall", {0.0f, 1.0f, side * 20.5f}, {40.0f, 2.0f, 1.0f}, "materials/cyan.material");
        }

        // A ramp up to a platform, to roll balls down from.
        AddBlock("Platform", {-10.0f, 1.0f, -8.0f}, {6.0f, 2.0f, 6.0f}, "materials/cyan.material");
        AddBlock("Ramp", {-10.0f, 0.9f, -1.9f}, {4.0f, 0.4f, 7.0f}, "materials/cyan.material",
                 Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, 0.3f));
        for (int i = 0; i < 5; ++i) {
            AddBall({-11.5f + 0.75f * static_cast<float>(i), 2.5f, -9.0f}, 0.6f, "materials/pink.material");
        }

        // Steps the capsule can climb.
        for (int i = 0; i < 4; ++i) {
            const float height = 0.25f * static_cast<float>(i + 1);
            AddBlock("Step", {10.0f, height / 2.0f, 2.0f - 1.0f * static_cast<float>(i)}, {3.0f, height, 1.0f},
                     "materials/cyan.material");
        }

        constexpr int PyramidBase = 5;
        for (int row = 0; row < PyramidBase; ++row) {
            for (int i = 0; i < PyramidBase - row; ++i) {
                const float x = -2.0f + (static_cast<float>(i) + 0.5f * static_cast<float>(row)) * 1.02f;
                AddCrate({x, 0.5f + static_cast<float>(row), -6.0f});
            }
        }

        for (int i = 0; i < 8; ++i) {
            AddCrate({8.0f, 0.5f + static_cast<float>(i), -8.0f});
        }

        auto& player = CreateEntity("Player");
        player.GetTransform().SetPosition(SpawnPoint);
        player.AddComponent<Spot::CapsuleCollider>(0.4f, 1.8f).SetFriction(0.0f);
        player.AddComponent<Spot::Rigidbody>().SetMass(70.0f);
        m_player = &player.AddComponent<PlayerController>();

        auto& flashlight = player.CreateChild("Flashlight");
        flashlight.GetTransform().SetPosition({0.0f, PlayerController::EyeHeight, 0.0f});
        m_flashlight = &flashlight.AddComponent<Spot::SpotLight>(Spot::Color{1.0f, 0.95f, 0.8f}, 1.5f, 25.0f);
        m_flashlight->SetInnerConeAngle(Spot::ToRadians(12.0f));
        m_flashlight->SetOuterConeAngle(Spot::ToRadians(20.0f));
        m_flashlight->SetEnabled(false);
        m_player->SetFlashlight(&flashlight);
    }

    void OnUpdate(float deltaTime) override {
        constexpr float DayLength = 120.0f;
        const float speed = Spot::Input::IsKeyDown(Spot::Key::T) ? 20.0f : 1.0f;
        m_timeOfDay = std::fmod(m_timeOfDay + deltaTime * speed / DayLength, 1.0f);
        UpdateSun();

        // Back to the start after falling off the world.
        if (m_player && m_player->GetEntity().GetTransform().GetWorldPosition().y < -20.0f) {
            m_player->GetEntity().GetTransform().SetPosition(SpawnPoint);
            m_player->GetEntity().GetComponent<Spot::Rigidbody>()->SetLinearVelocity({});
        }
    }

private:
    void AddBlock(const char* name, Spot::Vector3 position, Spot::Vector3 size, const char* material,
                  const Spot::Quaternion& rotation = {}) {
        auto& block = CreateEntity(name);
        block.GetTransform().SetPosition(position);
        block.GetTransform().SetRotation(rotation);
        block.GetTransform().SetScale(size);
        block.AddComponent<Spot::MeshRenderer>(Load<Spot::Mesh>(CubeMesh), Load<Spot::Material>(material));
        block.AddComponent<Spot::BoxCollider>();
    }

    void AddCrate(Spot::Vector3 position) {
        auto& crate = CreateEntity("Crate");
        crate.GetTransform().SetPosition(position);
        crate.AddComponent<Spot::MeshRenderer>(Load<Spot::Mesh>(CubeMesh),
                                               Load<Spot::Material>("materials/violet.material"));
        crate.AddComponent<Spot::BoxCollider>();
        crate.AddComponent<Spot::Rigidbody>().SetMass(5.0f);
    }

    Spot::Entity& AddBall(Spot::Vector3 position, float diameter, const char* material) {
        auto& ball = CreateEntity("Ball");
        ball.GetTransform().SetPosition(position);
        ball.GetTransform().SetScale({diameter, diameter, diameter});
        ball.AddComponent<Spot::MeshRenderer>(Load<Spot::Mesh>(SphereMesh), Load<Spot::Material>(material));
        ball.AddComponent<Spot::SphereCollider>().SetRestitution(0.4f);
        ball.AddComponent<Spot::Rigidbody>().SetMass(3.0f);
        return ball;
    }

    // The sun rises in the east (+X) at 6:00, peaks at noon and sets in the west at 18:00. The sky follows it, and
    // lights the scene through the air, so the light dims and reddens on its own as the sun sets.
    void UpdateSun() {
        const float angle = (m_timeOfDay - 0.25f) * 2.0f * Spot::Pi;
        const Spot::Vector3 toSun = Spot::Normalize(Spot::Vector3{std::cos(angle), std::sin(angle), 0.4f});
        m_sun->GetEntity().GetTransform().SetRotation(Spot::Quaternion::LookRotation(-toSun));
    }

    void AddLamp(Spot::Vector3 position, Spot::Color color) {
        auto& lamp = CreateEntity("Lamp");
        lamp.GetTransform().SetPosition(position);
        lamp.AddComponent<Spot::PointLight>(color, 1.5f, 8.0f);
    }

    PlayerController* m_player = nullptr;
    Spot::SpotLight* m_flashlight = nullptr;
    Spot::DirectionalLight* m_sun = nullptr;
    // Mid-morning.
    float m_timeOfDay = 0.375f;
};

} // namespace

class Sandbox final : public Spot::Application {
public:
    // In development the sandbox imports its sources from the repository, into "imported" next to the executable,
    // which is all a shipped build would need.
    Sandbox()
        : Application({.window = {.title = "Spot Sandbox"},
                       .assets = {.sourceDirectory = SPOT_SANDBOX_ASSETS_DIR},
                       .logFile = "sandbox.log",
                       .maxFrameRate = 60}) {}

protected:
    void OnStart() override {
        Spot::Log::Info("Spot {}", Spot::GetVersionString());

        auto& device = GetGraphicsDevice();
        m_renderer2D = std::make_unique<Spot::Renderer2D>(device);
        // Four samples per pixel smooth the edges of the 3D view, where the GPU supports them.
        const bool multisampled = device.SupportsSampleCount(device.GetWindowFormat(), 4) &&
                                  device.SupportsSampleCount(Spot::TextureFormat::Depth32Float, 4);
        m_sampleCount = multisampled ? 4 : 1;
        m_renderer3D = std::make_unique<Spot::Renderer3D>(device, Spot::Renderer3DConfig{.sampleCount = m_sampleCount});
        m_font = Load<Spot::Font>("fonts/FiraSans-Regular.ttf");

        LoadScene<PlaygroundScene>();
        GetWindow().SetMouseCaptured(true);
    }

    void OnUpdate(float deltaTime) override {
        namespace Input = Spot::Input;
        using Spot::Key;

        // Escape frees the mouse first, and quits once it is free; a click captures it again.
        auto& window = GetWindow();
        if (Input::IsKeyPressed(Key::Escape)) {
            if (window.IsMouseCaptured()) {
                window.SetMouseCaptured(false);
            } else {
                Quit();
            }
        }
        auto* scene = dynamic_cast<PlaygroundScene*>(GetScene());
        if (Input::IsKeyPressed(Key::R)) {
            scene = &LoadScene<PlaygroundScene>();
        }
        if (Input::IsMouseButtonPressed(Spot::MouseButton::Left)) {
            if (!window.IsMouseCaptured()) {
                window.SetMouseCaptured(true);
            } else if (scene) {
                scene->Shoot();
            }
        }
        if (scene && Input::IsKeyPressed(Key::F)) {
            scene->ToggleFlashlight();
        }
        if (scene && scene->GetPlayer() && window.IsMouseCaptured()) {
            scene->GetPlayer()->Look(Input::GetMouseDelta());
        }

        ++m_frames;
        m_elapsed += deltaTime;
        if (m_elapsed >= 1.0f) {
            const int minutes = scene ? static_cast<int>(scene->GetTimeOfDay() * 24.0f * 60.0f) : 0;
            m_status = std::format("{:02}:{:02} | {} FPS, {} bodies, {} draw calls", minutes / 60, minutes % 60,
                                   m_frames, scene ? scene->GetPhysics().GetBodyCount() : 0, m_stats3D.drawCalls);
            m_frames = 0;
            m_elapsed -= 1.0f;
        }
    }

    void OnRender(Spot::RenderContext& context) override {
        const Spot::Vector2 view = GetWindow().GetSize();
        Render3D(context, view);

        // The HUD has its own pass, without the depth target of the 3D view, which Renderer2D does not draw with.
        auto hud = context.BeginRenderPass();
        RenderHud(hud, view);
    }

private:
    void Render3D(Spot::RenderContext& context, Spot::Vector2 view) {
        // The targets must match the frame, whose size changes with the window. With several samples, the view is
        // drawn into a multisampled target that the pass resolves into the window.
        auto& device = GetGraphicsDevice();
        const auto width = static_cast<std::uint32_t>(context.GetWidth());
        const auto height = static_cast<std::uint32_t>(context.GetHeight());
        if (!m_depth || m_depth->GetWidth() != width || m_depth->GetHeight() != height) {
            m_depth = device.CreateTexture({.width = width,
                                            .height = height,
                                            .format = Spot::TextureFormat::Depth32Float,
                                            .renderTarget = true,
                                            .sampleCount = m_sampleCount});
            m_samples = m_sampleCount > 1 ? device.CreateTexture({.width = width,
                                                                  .height = height,
                                                                  .format = device.GetWindowFormat(),
                                                                  .renderTarget = true,
                                                                  .sampleCount = m_sampleCount})
                                          : nullptr;
        }
        if (!m_depth || (m_sampleCount > 1 && !m_samples)) {
            return;
        }

        const auto* scene = dynamic_cast<PlaygroundScene*>(GetScene());
        // The scene's sky covers the whole view, so the clear color never shows.
        auto pass = context.BeginRenderPass({.target = m_samples.get(),
                                             .depthTarget = m_depth.get(),
                                             .clearDepth = 1.0f,
                                             .resolveToWindow = m_samples != nullptr});
        if (!m_renderer3D || !scene || !scene->GetPlayer()) {
            return;
        }

        const PlayerController& player = *scene->GetPlayer();
        Spot::Camera3D camera;
        camera.position = player.GetEntity().GetTransform().GetWorldPosition() +
                          Spot::Vector3{0.0f, PlayerController::EyeHeight, 0.0f};
        camera.rotation = player.GetViewRotation();
        camera.fieldOfView = Spot::ToRadians(75.0f);

        m_renderer3D->Begin(pass, camera, view);
        m_renderer3D->DrawScene(*GetScene());
        m_renderer3D->End();
        m_stats3D = m_renderer3D->GetStats();
    }

    // The HUD uses a camera that puts world (0, 0) at the window's top-left corner, so it stays fixed on screen.
    void RenderHud(Spot::RenderPass& pass, Spot::Vector2 view) {
        if (!m_renderer2D || !m_font) {
            return;
        }
        Spot::Camera2D screen;
        screen.position = {view.x / 2.0f, -view.y / 2.0f};
        const float line = m_font->GetLineHeight();
        const Spot::Vector2 center = {view.x / 2.0f, -view.y / 2.0f};
        const Spot::Color crosshair{1.0f, 1.0f, 1.0f, 0.8f};

        m_renderer2D->Begin(pass, screen, view);
        m_renderer2D->DrawQuad(center, {14.0f, 2.0f}, crosshair);
        m_renderer2D->DrawQuad(center, {2.0f, 14.0f}, crosshair);
        m_renderer2D->DrawQuad({230.0f, -40.0f}, {460.0f, 80.0f}, Spot::Color{0.0f, 0.0f, 0.0f, 0.6f});
        m_renderer2D->DrawString(*m_font, m_status, {12.0f, -10.0f}, Spot::Color::FromSrgb(0.7f, 0.9f, 0.7f));
        m_renderer2D->DrawString(*m_font, "WASD: move | Shift: run | Space: jump | Click: shoot | R: reset",
                                 {12.0f, -10.0f - line}, Spot::Color::FromSrgb(0.8f, 0.8f, 0.8f), 0.8f);
        m_renderer2D->DrawString(*m_font, "F: flashlight | T: speed up time | Esc: free the mouse, then quit",
                                 {12.0f, -10.0f - 1.8f * line}, Spot::Color::FromSrgb(0.8f, 0.8f, 0.8f), 0.8f);
        m_renderer2D->End();
    }

    std::unique_ptr<Spot::Renderer2D> m_renderer2D;
    std::unique_ptr<Spot::Renderer3D> m_renderer3D;
    std::unique_ptr<Spot::Texture> m_depth;
    std::unique_ptr<Spot::Texture> m_samples;
    std::uint32_t m_sampleCount = 1;
    Spot::AssetRef<Spot::Font> m_font;
    Spot::Renderer3DStats m_stats3D;
    std::string m_status;
    int m_frames = 0;
    float m_elapsed = 0.0f;
};

int main() {
    Sandbox sandbox;
    return sandbox.Run();
}
