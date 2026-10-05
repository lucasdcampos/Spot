#include "panels/asset_browser_panel.h"

#include "imgui/icons.h"

namespace Spot::Editor {

AssetBrowserPanel::AssetBrowserPanel() : Panel("Assets", ICON_FA_FOLDER_OPEN "  Assets") {
}

void AssetBrowserPanel::OnDraw() {
    DrawEmptyState(ICON_FA_FOLDER_OPEN, "Open a project to browse its assets.");
}

} // namespace Spot::Editor
