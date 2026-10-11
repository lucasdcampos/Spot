#pragma once

#include "core/console_log.h"
#include "panels/panel.h"

#include <spot.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Spot::Editor {

// Log messages of the editor and the game, filtered by level, source and text, with selection and copying.
class ConsolePanel final : public Panel {
public:
    explicit ConsolePanel(std::shared_ptr<ConsoleLog> log);

    // Brings the console's own copy up to date with the log; drawing does it too.
    void Sync();
    const std::vector<ConsoleRow>& GetRows() const { return m_rows; }
    ConsoleFilter& GetFilter() { return m_filter; }
    void SetFilter(ConsoleFilter filter);
    void ExecuteCommand(std::string_view line);
    const std::string& GetCommandLine() const { return m_commandInput.GetLine(); }

protected:
    void OnDraw() override;

private:
    void DrawToolbar();
    void DrawCommandLine();
    static int OnCommandInput(ImGuiInputTextCallbackData* data);
    void DrawRows();
    void DrawRowContextMenu(const ConsoleEntry& entry);
    void Select(std::size_t row);
    void CopySelection() const;
    void Clear();

    std::shared_ptr<ConsoleLog> m_log;
    std::vector<ConsoleEntry> m_entries;
    std::vector<ConsoleRow> m_rows;
    std::vector<std::string> m_sources;
    ConsoleFilter m_filter;
    std::uint64_t m_lastSequence = 0;
    std::uint64_t m_clearCount = 0;
    bool m_rowsDirty = true;
    // Selected messages by sequence, so the selection survives filtering and new messages.
    std::set<std::uint64_t> m_selection;
    std::uint64_t m_selectionAnchor = 0;
    bool m_scrollToBottom = true;
    InputState m_commandState;
    ConsoleInput m_commandInput{Console::Get(), m_commandState};
    std::string m_commandText;
};

} // namespace Spot::Editor
