#include <spot.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Defined by SPOT_GAME_MODULE() in src/game.cpp: it registers the sandbox's components, as it does when the editor
// loads the same code as a module.
extern "C" void SpotLoadGameModule();

// Runs the sandbox project, which the editor opens too, as the player would: its startup scene through the scene's
// main camera, with the scene's interface over it. Everything the game does is in the project's components.
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
        // Four samples per pixel smooth the edges of the 3D view, where the GPU supports them.
        const bool multisampled = device.SupportsSampleCount(device.GetWindowFormat(), 4) &&
                                  device.SupportsSampleCount(Spot::TextureFormat::Depth32Float, 4);
        m_sampleCount = multisampled ? 4 : 1;
        m_renderer3D = std::make_unique<Spot::Renderer3D>(device, Spot::Renderer3DConfig{.sampleCount = m_sampleCount});
        m_renderer2D = std::make_unique<Spot::Renderer2D>(device, Spot::Renderer2DConfig{.sampleCount = m_sampleCount});

        auto scene = GetAssets().InstantiateScene(m_project.GetSettings().startupScene);
        if (!scene) {
            Spot::Log::Error("The sandbox's startup scene could not be loaded");
            Quit();
            return;
        }
        LoadScene(std::move(scene));
    }

    void OnRender(Spot::RenderContext& context) override {
        if (!UpdateTargets(context)) {
            return;
        }
        std::vector<Spot::Scene*> scenes = GetScenes();
        scenes.insert(scenes.begin(), &GetPersistentScene());
        const Spot::Camera* camera = nullptr;
        for (Spot::Scene* scene : scenes) {
            camera = camera ? camera : Spot::FindMainCamera(*scene);
        }

        // The scenes' skies cover the whole view, so the clear color never shows. With several samples, the view is
        // drawn into a multisampled target that each pass resolves into the window, keeping the samples for the next.
        Spot::Texture* samples = m_samples.get();
        m_renderer3D->Begin(context,
                            {.target = samples,
                             .depthTarget = m_depth.get(),
                             .clearDepth = 1.0f,
                             .resolveToWindow = samples != nullptr,
                             .keepSamples = samples != nullptr},
                            camera ? camera->GetCamera3D() : Spot::Camera3D{});
        for (Spot::Scene* scene : scenes) {
            m_renderer3D->DrawScene(*scene);
        }
        m_renderer3D->End();

        // The interface has its own pass, without the depth target of the 3D view, which Renderer2D does not draw
        // with.
        const Spot::Vector2 size{static_cast<float>(context.GetWidth()), static_cast<float>(context.GetHeight())};
        auto pass = context.BeginRenderPass({.target = samples, .resolveToWindow = samples != nullptr});
        m_renderer2D->Begin(pass, Spot::Scene::ScreenCamera(size), size);
        for (Spot::Scene* scene : scenes) {
            scene->DrawUi(*m_renderer2D, size);
        }
        m_renderer2D->End();
    }

private:
    // The targets must match the frame, whose size changes with the window.
    bool UpdateTargets(Spot::RenderContext& context) {
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
        return m_renderer3D && m_renderer2D && m_depth && (m_sampleCount == 1 || m_samples);
    }

    const Spot::Project& m_project;
    std::unique_ptr<Spot::Renderer3D> m_renderer3D;
    std::unique_ptr<Spot::Renderer2D> m_renderer2D;
    std::unique_ptr<Spot::Texture> m_depth;
    std::unique_ptr<Spot::Texture> m_samples;
    std::uint32_t m_sampleCount = 1;
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
