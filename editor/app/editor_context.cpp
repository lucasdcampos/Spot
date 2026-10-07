#include "app/editor_context.h"

#include "scene/scene_workspace.h"

namespace Spot::Editor {

SceneDocument* EditorContext::GetActiveDocument() {
    return m_workspace ? m_workspace->GetActiveDocument() : nullptr;
}

} // namespace Spot::Editor
