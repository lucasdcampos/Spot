#include "panels/inspector_panel.h"

#include "imgui/icons.h"

namespace Spot::Editor {

InspectorPanel::InspectorPanel(EditorContext& context)
    : Panel("Inspector", ICON_FA_CIRCLE_INFO "  Inspector"), m_context(context), m_entities(context),
      m_assets(context) {
}

CommandHistory* InspectorPanel::GetFocusedHistory() {
    return m_focused && m_showingAsset ? m_assets.GetHistory() : nullptr;
}

void InspectorPanel::OnDraw() {
    FinishEdit();
    // Menus take the focus while open, so Undo in the Edit menu still knows what the inspector had.
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    }
    m_showingAsset = false;
    EditorProject* project = m_context.GetProject();
    if (const AssetId asset = m_context.GetInspectedAsset(); project && asset.IsValid()) {
        m_showingAsset = true;
        m_assets.Draw(*project, asset);
        return;
    }
    SceneDocument* document = m_context.GetActiveDocument();
    if (!document || !document->GetScene().GetEntity(document->GetSelection().GetPrimary())) {
        DrawEmptyState(ICON_FA_CIRCLE_INFO, "Select an entity or an asset to see its properties.");
        return;
    }
    m_entities.Draw(*document);
}

} // namespace Spot::Editor
