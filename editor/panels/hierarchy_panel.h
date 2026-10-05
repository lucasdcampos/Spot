#pragma once

#include "panels/panel.h"

namespace Spot::Editor {

// The entities of the active scene, as a tree.
class HierarchyPanel final : public Panel {
public:
    HierarchyPanel();

protected:
    void OnDraw() override;
};

} // namespace Spot::Editor
