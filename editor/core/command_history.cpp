#include "core/command_history.h"

#include <algorithm>
#include <utility>

namespace Spot::Editor {

bool CommandGroup::Execute() {
    bool changed = false;
    for (const auto& command : m_commands) {
        changed = command->Execute() || changed;
    }
    return changed;
}

void CommandGroup::Undo() {
    for (auto command = m_commands.rbegin(); command != m_commands.rend(); ++command) {
        (*command)->Undo();
    }
}

CommandHistory::CommandHistory(std::size_t limit) : m_limit(std::max<std::size_t>(limit, 1)) {
}

bool CommandHistory::Execute(std::unique_ptr<Command> command) {
    if (!command->Execute()) {
        return false;
    }
    if (m_group) {
        m_group->Add(std::move(command));
        return true;
    }
    Push(std::move(command));
    return true;
}

void CommandHistory::Push(std::unique_ptr<Command> command) {
    // A saved state that was undone past can never come back once new changes are made.
    if (m_savedState && std::ranges::any_of(m_redo, [&](const Entry& entry) { return entry.state == *m_savedState; })) {
        m_savedState.reset();
    }
    m_redo.clear();

    // The saved state must stay reachable, so the command that made it is never changed by a merge.
    if (!m_undo.empty() && m_undo.back().state != m_savedState && m_undo.back().command->Merge(*command)) {
        m_undo.back().state = m_nextState++;
        return;
    }
    m_undo.push_back({std::move(command), m_nextState++});
    if (m_undo.size() > m_limit) {
        m_baseState = m_undo.front().state;
        m_undo.erase(m_undo.begin());
    }
}

void CommandHistory::Undo() {
    if (m_undo.empty()) {
        return;
    }
    Entry entry = std::move(m_undo.back());
    m_undo.pop_back();
    entry.command->Undo();
    m_redo.push_back(std::move(entry));
}

void CommandHistory::Redo() {
    if (m_redo.empty()) {
        return;
    }
    Entry entry = std::move(m_redo.back());
    m_redo.pop_back();
    entry.command->Execute();
    m_undo.push_back(std::move(entry));
}

std::string CommandHistory::GetUndoName() const {
    return m_undo.empty() ? std::string() : m_undo.back().command->GetName();
}

std::string CommandHistory::GetRedoName() const {
    return m_redo.empty() ? std::string() : m_redo.back().command->GetName();
}

void CommandHistory::BeginGroup(std::string name) {
    if (m_groupDepth++ == 0) {
        m_group = std::make_unique<CommandGroup>(std::move(name));
    }
}

void CommandHistory::EndGroup() {
    if (m_groupDepth == 0 || --m_groupDepth > 0) {
        return;
    }
    // Its commands were executed as they came, so the group only needs to be kept.
    if (auto group = std::move(m_group); !group->IsEmpty()) {
        Push(std::move(group));
    }
}

void CommandHistory::MarkSaved() {
    m_savedState = GetCurrentState();
}

bool CommandHistory::IsDirty() const {
    return m_savedState != GetCurrentState();
}

void CommandHistory::Clear() {
    // The current state stays what it is, so the document stays as dirty or clean as it was.
    m_baseState = GetCurrentState();
    if (m_savedState && std::ranges::any_of(m_redo, [&](const Entry& entry) { return entry.state == *m_savedState; })) {
        m_savedState.reset();
    }
    m_undo.clear();
    m_redo.clear();
}

} // namespace Spot::Editor
