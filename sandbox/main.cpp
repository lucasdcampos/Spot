#include "day_cycle.h"
#include "player_controller.h"

#include <spot.h>

#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>

// Defined by SPOT_GAME_MODULE() in src/game.cpp: it registers the sandbox's components, as it does when the editor
// loads the same code as a module.
extern "C" void SpotLoadGameModule();

// Runs the sandbox project, which the editor opens too, with what only the sandbox has on top: the mouse captured for
// looking around, a HUD, and keys to reset or quit.
class Sandbox final : public Spot::Application {
public:
    // In development the sandbox imports the project's sources from the repository, into "imported" next to the
    // executable, which is all a shipped build would need.
    explicit Sandbox(const Spot::Project& project)
        : Application({.window = {.title = "Spot Sandbox"},
                       .assets = {.sourceDirectory = project.GetAssetsDirectory()},
                       .logFile = "sandbox.log",
                       .maxFrameRate = 60}),
          m_project(project) {}

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
        m_font = Spot::Assets::Load<Spot::Font>("fonts/FiraSans-Regular.ttf");

        if (!LoadStartupScene()) {
            Quit();
            return;
        }
        GetWindow().SetMouseCaptured(true);
    }

    void OnUpdate(float deltaTime) override {
        namespace Input = Spot::Input;
        using Spot::Key;

        // The player looks around only while the mouse is captured. The click that captures it is seen before the
        // player looks again, so it throws nothing.
        auto& window = GetWindow();
        if (auto* player = GetPlayer()) {
            player->SetLooking(window.IsMouseCaptured());
        }

        // Escape frees the mouse first, and quits once it is free; a click captures it again.
        if (Input::IsKeyPressed(Key::Escape)) {
            if (window.IsMouseCaptured()) {
                window.SetMouseCaptured(false);
            } else {
                Quit();
            }
        }
        if (Input::IsMouseButtonPressed(Spot::MouseButton::Left) && !window.IsMouseCaptured()) {
            window.SetMouseCaptured(true);
        }
        if (Input::IsKeyPressed(Key::R)) {
            LoadStartupScene();
        }

        ++m_frames;
        m_elapsed += deltaTime;
        if (m_elapsed >= 1.0f) {
            const Spot::Scene* scene = GetScene();
            const DayCycle* day = scene ? scene->FindComponent<DayCycle>() : nullptr;
            const int minutes = day ? static_cast<int>(day->timeOfDay * 24.0f * 60.0f) : 0;
            m_status = std::format("{:02}:{:02} | {} FPS, {} bodies, {} + {} shadow draw calls", minutes / 60,
                                   minutes % 60, m_frames, scene ? scene->GetPhysics().GetBodyCount() : 0,
                                   m_stats3D.drawCalls, m_stats3D.shadowDrawCalls);
            m_frames = 0;
            m_elapsed -= 1.0f;
        }
    }

    void OnRender(Spot::RenderContext& context) override {
        const Spot::Vector2 view = GetWindow().GetSize();
        Render3D(context);

        // The HUD has its own pass, without the depth target of the 3D view, which Renderer2D does not draw with.
        auto hud = context.BeginRenderPass();
        RenderHud(hud, view);
    }

private:
    bool LoadStartupScene() {
        auto scene = GetAssets().InstantiateScene(m_project.GetSettings().startupScene);
        if (!scene) {
            Spot::Log::Error("The sandbox's startup scene could not be loaded");
            return false;
        }
        LoadScene(std::move(scene));
        return true;
    }

    PlayerController* GetPlayer() const {
        const Spot::Scene* scene = GetScene();
        return scene ? scene->FindComponent<PlayerController>() : nullptr;
    }

    void Render3D(Spot::RenderContext& context) {
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

        // The scene's sky covers the whole view, so the clear color never shows.
        const Spot::RenderPassDesc target{.target = m_samples.get(),
                                          .depthTarget = m_depth.get(),
                                          .clearDepth = 1.0f,
                                          .resolveToWindow = m_samples != nullptr};
        Spot::Scene* scene = GetScene();
        const Spot::Camera* camera = scene ? Spot::FindMainCamera(*scene) : nullptr;
        if (!m_renderer3D || !camera) {
            context.BeginRenderPass(target);
            return;
        }

        m_renderer3D->Begin(context, target, camera->GetCamera3D());
        m_renderer3D->DrawScene(*scene);
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
        m_renderer2D->DrawQuad({240.0f, -40.0f}, {480.0f, 80.0f}, Spot::Color{0.0f, 0.0f, 0.0f, 0.6f});
        m_renderer2D->DrawString(*m_font, m_status, {12.0f, -10.0f}, Spot::Color::FromSrgb(0.7f, 0.9f, 0.7f));
        m_renderer2D->DrawString(*m_font, "WASD: move | Shift: run | Space: jump | Click: shoot | R: reset",
                                 {12.0f, -10.0f - line}, Spot::Color::FromSrgb(0.8f, 0.8f, 0.8f), 0.8f);
        m_renderer2D->DrawString(*m_font,
                                 "F: flashlight | G: shadows | T: speed up time | Esc: free the mouse, then quit",
                                 {12.0f, -10.0f - 1.8f * line}, Spot::Color::FromSrgb(0.8f, 0.8f, 0.8f), 0.8f);
        m_renderer2D->End();
    }

    const Spot::Project& m_project;
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
    const std::optional<Spot::Project> project = Spot::Project::Load(SPOT_SANDBOX_PROJECT);
    if (!project) {
        return 1;
    }
    SpotLoadGameModule();
    Sandbox sandbox(*project);
    return sandbox.Run();
}
