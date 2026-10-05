#include "panels/hierarchy_panel.h"

#include "imgui/icons.h"

namespace Spot::Editor {

HierarchyPanel::HierarchyPanel() : Panel("Hierarchy", ICON_FA_LIST "  Hierarchy") {
}

void HierarchyPanel::OnDraw() {
    DrawEmptyState(ICON_FA_LIST, "Open a scene to see its entities.");
}

} // namespace Spot::Editor
