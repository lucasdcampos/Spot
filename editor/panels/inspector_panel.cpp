#include "panels/inspector_panel.h"

#include "imgui/icons.h"

namespace Spot::Editor {

InspectorPanel::InspectorPanel() : Panel("Inspector", ICON_FA_CIRCLE_INFO "  Inspector") {
}

void InspectorPanel::OnDraw() {
    DrawEmptyState(ICON_FA_CIRCLE_INFO, "Select an entity or an asset to see its properties.");
}

} // namespace Spot::Editor
