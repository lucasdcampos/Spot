#include "panels/console_panel.h"

#include "imgui/icons.h"

namespace Spot::Editor {

ConsolePanel::ConsolePanel() : Panel("Console", ICON_FA_TERMINAL "  Console") {
}

void ConsolePanel::OnDraw() {
    DrawEmptyState(ICON_FA_TERMINAL, "Messages from the editor and the game appear here.");
}

} // namespace Spot::Editor
