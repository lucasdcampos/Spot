#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Spot::Editor {

// A change that can be undone. Commands refer to what they change by ids, never by pointers, so they stay valid
// while entities are destroyed and brought back, and while game code reloads.
class Command {
public:
    virtual ~Command() = default;

    // Names the change in menus, such as "Rename Entity".
    virtual std::string GetName() const = 0;
    // Makes the change, the first time and again on each redo. Returns whether anything changed; a command that
    // changed nothing the first time is not kept.
    virtual bool Execute() = 0;
    virtual void Undo() = 0;
    // Folds the next command into this one when both are steps of one change, such as moving the same entity twice;
    // returns whether it did.
    virtual bool Merge(const Command& /*next*/) { return false; }
};

// Several commands done and undone as one, undone in reverse order.
class CommandGroup final : public Command {
public:
    explicit CommandGroup(std::string name) : m_name(std::move(name)) {}

    void Add(std::unique_ptr<Command> command) { m_commands.push_back(std::move(command)); }
    bool IsEmpty() const { return m_commands.empty(); }

    std::string GetName() const override { return m_name; }
    bool Execute() override;
    void Undo() override;

private:
    std::string m_name;
    std::vector<std::unique_ptr<Command>> m_commands;
};

} // namespace Spot::Editor
