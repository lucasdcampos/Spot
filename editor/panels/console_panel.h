#pragma once

#include "panels/panel.h"

namespace Spot::Editor {

// Log messages of the editor and the game.
class ConsolePanel final : public Panel {
public:
    ConsolePanel();

protected:
    void OnDraw() override;
};

} // namespace Spot::Editor
