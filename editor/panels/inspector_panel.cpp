#include "panels/inspector_panel.h"

#include "imgui/icons.h"

namespace Spot::Editor {

InspectorPanel::InspectorPanel(EditorContext& context)
    : Panel("Inspector", ICON_FA_CIRCLE_INFO "  Inspector"), m_context(context), m_entities(context) {
}

void InspectorPanel::OnDraw() {
    m_entities.FinishEdit();
    SceneDocument* document = m_context.GetActiveDocument();
    if (!document || !document->GetScene().GetEntity(document->GetSelection().GetPrimary())) {
        DrawEmptyState(ICON_FA_CIRCLE_INFO, "Select an entity to see its components.");
        return;
    }
    m_entities.Draw(*document);
}

} // namespace Spot::Editor
