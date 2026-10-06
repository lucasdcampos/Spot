#include "core/command_history.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

namespace {

// Adds an amount to a value; amounts of zero change nothing. Steps marked mergeable fold into each other.
class AddCommand final : public Spot::Editor::Command {
public:
    AddCommand(int& value, int amount, bool mergeable = false)
        : m_value(value), m_amount(amount), m_mergeable(mergeable) {}

    std::string GetName() const override { return "Add " + std::to_string(m_amount); }

    bool Execute() override {
        m_value += m_amount;
        return m_amount != 0;
    }

    void Undo() override { m_value -= m_amount; }

    bool Merge(const Command& next) override {
        const auto* other = dynamic_cast<const AddCommand*>(&next);
        if (!m_mergeable || !other || !other->m_mergeable) {
            return false;
        }
        m_amount += other->m_amount;
        return true;
    }

private:
    int& m_value;
    int m_amount;
    bool m_mergeable;
};

} // namespace

TEST_CASE("Commands are undone and redone in order", "[editor][history]") {
    int value = 0;
    Spot::Editor::CommandHistory history;

    CHECK(history.Execute(std::make_unique<AddCommand>(value, 1)));
    CHECK(history.Execute(std::make_unique<AddCommand>(value, 10)));
    CHECK(value == 11);
    CHECK(history.GetUndoName() == "Add 10");

    history.Undo();
    CHECK(value == 1);
    CHECK(history.GetRedoName() == "Add 10");
    history.Undo();
    CHECK(value == 0);
    CHECK_FALSE(history.CanUndo());
    history.Undo();
    CHECK(value == 0);

    history.Redo();
    history.Redo();
    CHECK(value == 11);
    CHECK_FALSE(history.CanRedo());
}

TEST_CASE("Commands that change nothing are not kept", "[editor][history]") {
    int value = 0;
    Spot::Editor::CommandHistory history;

    CHECK_FALSE(history.Execute(std::make_unique<AddCommand>(value, 0)));

    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.IsDirty());
}

TEST_CASE("A new command forgets what could be redone", "[editor][history]") {
    int value = 0;
    Spot::Editor::CommandHistory history;
    history.Execute(std::make_unique<AddCommand>(value, 1));
    history.Undo();

    history.Execute(std::make_unique<AddCommand>(value, 5));

    CHECK_FALSE(history.CanRedo());
    CHECK(value == 5);
}

TEST_CASE("The history knows whether the document changed since it was saved", "[editor][history]") {
    int value = 0;
    Spot::Editor::CommandHistory history;
    CHECK_FALSE(history.IsDirty());

    history.Execute(std::make_unique<AddCommand>(value, 1));
    CHECK(history.IsDirty());
    history.Undo();
    CHECK_FALSE(history.IsDirty());
    history.Redo();
    history.MarkSaved();
    CHECK_FALSE(history.IsDirty());

    history.Execute(std::make_unique<AddCommand>(value, 2));
    CHECK(history.IsDirty());
    history.Undo();
    CHECK_FALSE(history.IsDirty());

    // Once the saved state is undone past and something else is done, it can never come back.
    history.Undo();
    history.Execute(std::make_unique<AddCommand>(value, 3));
    history.Undo();
    CHECK(history.IsDirty());
}

TEST_CASE("Steps of one change merge, but never into the saved state", "[editor][history]") {
    int value = 0;
    Spot::Editor::CommandHistory history;

    history.Execute(std::make_unique<AddCommand>(value, 1, true));
    history.Execute(std::make_unique<AddCommand>(value, 2, true));
    CHECK(history.GetUndoCount() == 1);
    CHECK(history.GetUndoName() == "Add 3");

    history.MarkSaved();
    history.Execute(std::make_unique<AddCommand>(value, 4, true));
    CHECK(history.GetUndoCount() == 2);
    CHECK(history.IsDirty());
    history.Undo();
    CHECK(value == 3);
    CHECK_FALSE(history.IsDirty());
}

TEST_CASE("Groups undo as one step", "[editor][history]") {
    int value = 0;
    Spot::Editor::CommandHistory history;

    history.BeginGroup("Paste");
    history.Execute(std::make_unique<AddCommand>(value, 1));
    history.BeginGroup("Inner");
    history.Execute(std::make_unique<AddCommand>(value, 2));
    history.EndGroup();
    history.Execute(std::make_unique<AddCommand>(value, 4));
    history.EndGroup();

    CHECK(value == 7);
    REQUIRE(history.GetUndoCount() == 1);
    CHECK(history.GetUndoName() == "Paste");
    history.Undo();
    CHECK(value == 0);
    history.Redo();
    CHECK(value == 7);

    history.BeginGroup("Nothing");
    history.EndGroup();
    CHECK(history.GetUndoCount() == 1);
}

TEST_CASE("The history forgets its oldest commands beyond its limit", "[editor][history]") {
    int value = 0;
    Spot::Editor::CommandHistory history(3);
    for (int i = 0; i < 5; ++i) {
        history.Execute(std::make_unique<AddCommand>(value, 1));
    }

    CHECK(history.GetUndoCount() == 3);
    while (history.CanUndo()) {
        history.Undo();
    }
    CHECK(value == 2);
    // Undoing all that is left does not reach the saved state, which is gone.
    CHECK(history.IsDirty());
}

TEST_CASE("Clearing the history keeps whether the document is saved", "[editor][history]") {
    int value = 0;
    Spot::Editor::CommandHistory history;
    history.Execute(std::make_unique<AddCommand>(value, 1));

    history.Clear();

    CHECK_FALSE(history.CanUndo());
    CHECK(history.IsDirty());
    history.MarkSaved();
    history.Clear();
    CHECK_FALSE(history.IsDirty());
}
