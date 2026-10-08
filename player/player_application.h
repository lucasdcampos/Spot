#pragma once

#include <spot.h>

#include <cstdint>
#include <memory>

namespace Spot::Player {

// The player's own messages, shown as "Player".
Logger& GetPlayerLogger();

struct PlayerOptions {
    // Without graphics, nothing is drawn and no GPU is needed, as for tests.
    bool graphics = true;
    // Quits after this many frames when above 0, as tests do.
    int frames = 0;
};

// Runs a project as its game: its startup scene, seen through the scene's main camera. The game module with the
// project's components must be loaded already.
class PlayerApplication final : public Application {
public:
    PlayerApplication(const Project& project, PlayerOptions options);

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnRender(RenderContext& context) override;

private:
    // Makes the targets the size of the frame, which changes with the window.
    bool UpdateTargets(RenderContext& context);

    const Project& m_project;
    PlayerOptions m_options;
    int m_frame = 0;
    std::unique_ptr<Renderer3D> m_renderer3D;
    std::unique_ptr<Renderer2D> m_renderer2D;
    std::uint32_t m_sampleCount = 1;
    std::unique_ptr<Texture> m_depth;
    std::unique_ptr<Texture> m_samples;
    bool m_warnedNoCamera = false;
};

} // namespace Spot::Player
