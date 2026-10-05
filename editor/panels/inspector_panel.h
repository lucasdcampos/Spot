#pragma once

#include "panels/panel.h"

namespace Spot::Editor {

// The properties of the selected entity or asset.
class InspectorPanel final : public Panel {
public:
    InspectorPanel();

protected:
    void OnDraw() override;
};

} // namespace Spot::Editor
