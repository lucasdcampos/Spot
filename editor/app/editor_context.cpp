#include "app/editor_context.h"

#include "scene/scene_workspace.h"

namespace Spot::Editor {

SceneDocument* EditorContext::GetActiveDocument() {
    return m_workspace ? m_workspace->GetActiveDocument() : nullptr;
}

void EditorContext::InspectAsset(AssetId asset) {
    m_inspectedAsset = asset;
    const SceneDocument* document = GetActiveDocument();
    m_inspectedDocument = document ? document->GetId() : 0;
    m_inspectedSelection = document ? document->GetSelection().GetVersion() : 0;
}

AssetId EditorContext::GetInspectedAsset() {
    if (!m_inspectedAsset.IsValid()) {
        return {};
    }
    // Selecting entities, or another scene, shows them instead; so does the asset going away.
    const SceneDocument* document = GetActiveDocument();
    const bool selectionChanged = document && (document->GetId() != m_inspectedDocument ||
                                               document->GetSelection().GetVersion() != m_inspectedSelection);
    if (selectionChanged || !m_project || !m_project->GetAssetDatabase().Find(m_inspectedAsset)) {
        m_inspectedAsset = {};
    }
    return m_inspectedAsset;
}

} // namespace Spot::Editor
