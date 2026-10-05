#include "panels/asset_browser_panel.h"

#include "imgui/icons.h"

#include <format>

namespace Spot::Editor {

AssetBrowserPanel::AssetBrowserPanel(EditorContext& context)
    : Panel("Assets", ICON_FA_FOLDER_OPEN "  Assets"), m_context(context) {
}

void AssetBrowserPanel::OnDraw() {
    EditorProject* project = m_context.GetProject();
    if (!project) {
        DrawEmptyState(ICON_FA_FOLDER_OPEN, "Open a project to browse its assets.");
        return;
    }
    const std::size_t count = project->GetAssetDatabase().GetAssets().size();
    DrawEmptyState(ICON_FA_FOLDER_OPEN,
                   std::format("{} assets in {}", count, PathToUtf8(project->GetProject().GetAssetsDirectory())));
}

} // namespace Spot::Editor
