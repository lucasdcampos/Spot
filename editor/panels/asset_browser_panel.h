#pragma once

#include "app/editor_context.h"
#include "panels/panel.h"

namespace Spot::Editor {

// The project's assets, by folder.
class AssetBrowserPanel final : public Panel {
public:
    explicit AssetBrowserPanel(EditorContext& context);

protected:
    void OnDraw() override;

private:
    EditorContext& m_context;
};

} // namespace Spot::Editor
