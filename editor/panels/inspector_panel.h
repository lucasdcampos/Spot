#pragma once

#include "app/editor_context.h"
#include "inspector/entity_inspector.h"
#include "panels/panel.h"

namespace Spot::Editor {

// The properties of the selected entities.
class InspectorPanel final : public Panel {
public:
    explicit InspectorPanel(EditorContext& context);

    // Puts a finished edit in the history; called each frame before anything can undo (see
    // EntityInspector::FinishEdit()).
    void FinishEdit() { m_entities.FinishEdit(); }
    // See EntityInspector::GetItemRect().
    std::optional<std::pair<ImVec2, ImVec2>> GetItemRect(std::string_view item) const {
        return m_entities.GetItemRect(item);
    }

protected:
    void OnDraw() override;

private:
    EditorContext& m_context;
    EntityInspector m_entities;
};

} // namespace Spot::Editor
