#include "spot/scene/rendering/render_module.h"

#include "spot/graphics/graphics_assets.h"
#include "spot/render/render_assets.h"
#include "spot/runtime/application.h"
#include "spot/runtime/console.h"
#include "spot/scene/rendering/rendering_components.h"
#include "spot/scene/rendering/scene_renderer.h"

namespace Spot {

RenderModule::RenderModule(RenderModuleConfig config) : m_config(config) {
}

RenderModule::~RenderModule() = default;

void RenderModule::OnStart() {
    Application& application = GetApplication();
    GraphicsDevice* device = application.HasGraphicsDevice() ? &application.GetGraphicsDevice() : nullptr;
    RegisterGraphicsAssets(application.GetAssets(), device);
    RegisterRenderAssets(application.GetAssets(), device);
    RegisterRenderComponents();
    m_console = std::make_unique<ConsoleRegistration>(Console::Get().RegisterVariable(
        "render.enabled", "Render the application's scene", {}, FormatConsoleValue(m_config.drawScene),
        [this] { return FormatConsoleValue(m_config.drawScene); },
        [this](std::string_view text) { return ParseConsoleValue(text, m_config.drawScene); }));
}

void RenderModule::OnStop() {
    m_console.reset();
    m_renderer.reset();
}

void RenderModule::OnRender(RenderContext& context) {
    Application& application = GetApplication();
    Scene* scene = application.GetScene();
    if (!m_config.drawScene || !scene) {
        return;
    }
    if (m_renderer && m_renderingRevision != application.GetRenderingRevision()) {
        m_renderer.reset();
    }
    if (!m_renderer) {
        SceneRendererConfig config;
        application.GetRenderingSettings().Apply(config);
        m_renderer = std::make_unique<SceneRenderer>(application.GetGraphicsDevice(), config);
        m_renderingRevision = application.GetRenderingRevision();
    }
    m_renderer->Render(context, *scene);
}

} // namespace Spot
