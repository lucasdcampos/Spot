#pragma once

#include "app/editor_context.h"
#include "panels/panel.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Spot::Editor {

// The open project's settings: its name, version and startup scene, and the sections of the engine's features. Changes
// are saved as soon as they are made.
class ProjectSettingsPanel final : public Panel {
public:
    explicit ProjectSettingsPanel(EditorContext& context);

    // Waits for the next control pressed (see EditorContext::GetInput()), which replaces the control of an input
    // action's binding, or with a part from 0 to 3, that part of a composite. Escape cancels.
    void CaptureBinding(std::size_t action, std::size_t binding, int part = -1);
    bool IsCapturing() const { return m_capture.has_value(); }

protected:
    void SetUpWindow() override;
    void OnDraw() override;

private:
    void Save();
    // The names of the entities' layers, and which collide with which.
    void DrawLayers(PhysicsSettings& physics);
    // The game's actions and their bindings.
    void DrawInput(InputSettings& input);
    void DrawBinding(InputSettings& input, std::size_t action, std::size_t binding);
    // A button showing the control, which captures another when clicked.
    void DrawCaptureButton(const InputControl& control, std::size_t action, std::size_t binding, int part);
    void UpdateCapture(InputSettings& input);

    EditorContext& m_context;
    bool m_sectionsChanged = false;

    struct Capture {
        std::size_t action;
        std::size_t binding;
        int part;
    };
    std::optional<Capture> m_capture;
};

} // namespace Spot::Editor
