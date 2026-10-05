#pragma once

#include "panels/panel.h"

namespace Spot::Editor {

// The project's assets, by folder.
class AssetBrowserPanel final : public Panel {
public:
    AssetBrowserPanel();

protected:
    void OnDraw() override;
};

} // namespace Spot::Editor
