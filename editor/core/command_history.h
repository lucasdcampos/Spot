#pragma once

#include "core/command.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Spot::Editor {

// The commands done to a document, to undo and redo them, and whether it changed since it was saved.
class CommandHistory {
public:
    static constexpr std::size_t DefaultLimit = 500;

    // Beyond the limit, the oldest commands are forgotten.
    explicit CommandHistory(std::size_t limit = DefaultLimit);

    // Executes the command and keeps it to undo, unless it changed nothing. Commands that can be undone again are
    // forgotten, as the document no longer is where they left it. Returns whether it changed anything.
    bool Execute(std::unique_ptr<Command> command);

    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    void Undo();
    void Redo();
    // Names of the next command to undo and to redo, for menus; empty when there is none.
    std::string GetUndoName() const;
    std::string GetRedoName() const;
    std::size_t GetUndoCount() const { return m_undo.size(); }
    std::size_t GetRedoCount() const { return m_redo.size(); }

    // Commands executed until EndGroup() become one, undone in a single step. Groups may nest; only the outermost
    // one counts.
    void BeginGroup(std::string name);
    void EndGroup();

    // Remembers the current state as the saved one.
    void MarkSaved();
    // Whether the document differs from when it was last saved, or was never saved when it began with changes.
    bool IsDirty() const;
    // Forgets every command; the document counts as saved when it was.
    void Clear();

private:
    struct Entry {
        std::unique_ptr<Command> command;
        // Tells states apart for the saved state: a merged command makes a new one.
        std::uint64_t state;
    };

    void Push(std::unique_ptr<Command> command);
    std::uint64_t GetCurrentState() const { return m_undo.empty() ? m_baseState : m_undo.back().state; }

    std::size_t m_limit;
    std::vector<Entry> m_undo;
    std::vector<Entry> m_redo;
    std::uint64_t m_nextState = 1;
    // The state below the oldest command kept: where undoing everything leads.
    std::uint64_t m_baseState = 0;
    std::optional<std::uint64_t> m_savedState = std::uint64_t{0};
    std::unique_ptr<CommandGroup> m_group;
    int m_groupDepth = 0;
};

} // namespace Spot::Editor
