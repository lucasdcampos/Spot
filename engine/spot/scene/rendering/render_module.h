#pragma once

#include "spot/runtime/module.h"

#include <cstdint>
#include <memory>

namespace Spot {

class SceneRenderer;
class ConsoleRegistration;

struct RenderModuleConfig {
    // Draws the active scene into the window, before the application's OnRender(), so a game needs no code to show
    // it. Tools that show scenes their own way, such as the editor, turn it off and keep the rest of the module.
    bool drawScene = true;
};

// Rendering for an application: registers the graphics and render asset loaders and the rendering components, and
// draws the active scene with a SceneRenderer made from the application's rendering settings.
class RenderModule final : public Module {
public:
    explicit RenderModule(RenderModuleConfig config = {});
    ~RenderModule() override;

    bool IsSceneDrawingEnabled() const { return m_config.drawScene; }
    void SetSceneDrawingEnabled(bool enabled) { m_config.drawScene = enabled; }

protected:
    void OnStart() override;
    void OnStop() override;
    void OnRender(RenderContext& context) override;

private:
    RenderModuleConfig m_config;
    // Made when there is first something to draw, so applications that draw no scene never pay for it, and again
    // when the rendering settings change.
    std::unique_ptr<SceneRenderer> m_renderer;
    std::uint64_t m_renderingRevision = 0;
    std::unique_ptr<ConsoleRegistration> m_console;
};

} // namespace Spot
