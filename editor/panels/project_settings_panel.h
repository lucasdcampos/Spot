#pragma once

#include "app/editor_context.h"
#include "panels/panel.h"

#include <string>

namespace Spot::Editor {

// The open project's settings: its name, version and startup scene, and the sections of the engine's features. Changes
// are saved as soon as they are made.
class ProjectSettingsPanel final : public Panel {
public:
    explicit ProjectSettingsPanel(EditorContext& context);

protected:
    void SetUpWindow() override;
    void OnDraw() override;

private:
    void Save();
    // The names of the entities' layers, and which collide with which.
    void DrawLayers(PhysicsSettings& physics);

    EditorContext& m_context;
    bool m_sectionsChanged = false;
};

} // namespace Spot::Editor
