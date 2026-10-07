#pragma once

#include "app/editor_context.h"
#include "inspector/asset_inspector.h"
#include "inspector/entity_inspector.h"
#include "panels/panel.h"

namespace Spot::Editor {

// The properties of the selected entities, or of the asset clicked last in the asset browser.
class InspectorPanel final : public Panel {
public:
    explicit InspectorPanel(EditorContext& context);

    // Puts a finished edit in the history; called each frame before anything can undo (see
    // EntityInspector::FinishEdit()).
    void FinishEdit() {
        m_entities.FinishEdit();
        m_assets.FinishEdit();
    }
    // The history Ctrl+Z undoes while the inspector has focus: the shown material's edits; null otherwise, when
    // the active scene's history is.
    CommandHistory* GetFocusedHistory();
    AssetInspector& GetAssetInspector() { return m_assets; }
    // See EntityInspector::GetItemRect().
    std::optional<std::pair<ImVec2, ImVec2>> GetItemRect(std::string_view item) const {
        return m_entities.GetItemRect(item);
    }

protected:
    void OnDraw() override;

private:
    EditorContext& m_context;
    EntityInspector m_entities;
    AssetInspector m_assets;
    bool m_focused = false;
    bool m_showingAsset = false;
};

} // namespace Spot::Editor
