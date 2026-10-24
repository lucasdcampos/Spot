#pragma once

#include "spot/runtime/module.h"

namespace Spot {

// The entity-based UI for an application: registers its components, routes the window's events to the active
// scene's canvases before the game sees them, and starts the window's text input while a text field is focused. The
// canvases draw with the scene, through SceneRenderer, so they show wherever the scene does.
class UiModule final : public Module {
protected:
    void OnStart() override;
    void OnStop() override;
    bool OnEvent(const Event& event) override;
    void OnUpdate(float deltaTime) override;

private:
    void UpdateTextInput();

    bool m_textInput = false;
};

} // namespace Spot
