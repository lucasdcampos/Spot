#include <spot.h>
#include <spot/runtime/console_input.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Console capture releases gameplay keys and restores the requested cursor lock", "[console][input]") {
    Spot::Console console;
    Spot::InputState state;
    state.SetCursorLocked(true);
    state.OnEvent(Spot::KeyPressedEvent{Spot::Key::W, false});
    REQUIRE(state.IsKeyDown(Spot::Key::W));
    {
        Spot::ConsoleInput input(console, state);
        CHECK(input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Grave, false}));
        CHECK(input.IsOpen());
        CHECK(state.IsCaptured());
        CHECK_FALSE(state.IsKeyDown(Spot::Key::W));
        CHECK(state.IsCursorLocked());
        CHECK_FALSE(state.IsCursorLockApplied());
        CHECK(input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left}));
        CHECK_FALSE(input.OnEvent(Spot::WindowResizeEvent{640, 480}));
        CHECK(input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Escape, false}));
        CHECK_FALSE(input.IsOpen());
        CHECK_FALSE(state.IsCaptured());
        CHECK(state.IsCursorLockApplied());
        input.SetOpen(true);
    }
    CHECK_FALSE(state.IsCaptured());
}

TEST_CASE("Console text uses UTF-8 events, completion, submission and draft-preserving history", "[console][input]") {
    Spot::Console console;
    Spot::CVar<int> count(console, "count", 1);
    Spot::InputState state;
    Spot::ConsoleInput input(console, state);
    input.SetOpen(true);
    input.OnEvent(Spot::TextInputEvent{"cou"});
    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Tab, false});
    CHECK(input.GetLine() == "count ");
    input.OnEvent(Spot::TextInputEvent{"7"});
    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Enter, false});
    CHECK(count.Get() == 7);
    CHECK(input.GetLine().empty());
    input.SetLine("count 9");
    input.Submit();
    input.SetLine("draft");
    input.BrowseHistory(-1);
    CHECK(input.GetLine() == "count 9");
    input.BrowseHistory(-1);
    CHECK(input.GetLine() == "count 7");
    input.BrowseHistory(1);
    CHECK(input.GetLine() == "count 9");
    input.BrowseHistory(1);
    CHECK(input.GetLine() == "draft");
    input.SetLine("a\xC3\xA9\xF0\x9F\x98\x80");
    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Backspace, false});
    CHECK(input.GetLine() == "a\xC3\xA9");
    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Backspace, false});
    CHECK(input.GetLine() == "a");
    input.OnEvent(Spot::WindowFocusEvent{false});
    CHECK_FALSE(input.IsOpen());
}

TEST_CASE("Closing a console consumes Escape instead of activating a gameplay pause binding", "[console][input]") {
    Spot::Console console;
    Spot::InputState state;
    Spot::ConsoleInput input(console, state);
    const auto dispatch = [&](const Spot::Event& event) {
        if (!input.OnEvent(event)) {
            state.OnEvent(event);
        }
    };
    dispatch(Spot::KeyPressedEvent{Spot::Key::Grave, false});
    REQUIRE(input.IsOpen());
    dispatch(Spot::KeyPressedEvent{Spot::Key::Escape, false});
    CHECK_FALSE(input.IsOpen());
    CHECK_FALSE(state.IsKeyPressed(Spot::Key::Escape));
    CHECK_FALSE(state.IsKeyDown(Spot::Key::Escape));
    dispatch(Spot::KeyReleasedEvent{Spot::Key::Escape});
    state.BeginFrame();
    dispatch(Spot::KeyPressedEvent{Spot::Key::W, false});
    CHECK(state.IsKeyDown(Spot::Key::W));
}

TEST_CASE("Applications route console events before gameplay and toggle platform text input", "[console][input]") {
    class InputScene final : public Spot::Scene {
    public:
        int keys = 0;

    protected:
        void OnEvent(const Spot::Event& event) override {
            if (event.Is<Spot::KeyPressedEvent>()) {
                ++keys;
            }
        }
    };
    class ConsoleApplication final : public Spot::Application {
    public:
        ConsoleApplication() : Application({.graphics = {.enabled = false}}) {}

    protected:
        void OnStart() override { LoadScene<InputScene>(); }
        void OnUpdate(float) override {
            auto* scene = static_cast<InputScene*>(GetScene());
            REQUIRE(scene);
            scene->keys = 0;
            Spot::Input::SetCursorLocked(true);
            DispatchEvent(Spot::KeyPressedEvent{Spot::Key::Grave, false});
            REQUIRE(GetConsoleInput());
            CHECK(GetConsoleInput()->IsOpen());
            CHECK(GetWindow().IsTextInputActive());
            CHECK(GetInputState().IsCaptured());
            DispatchEvent(Spot::KeyPressedEvent{Spot::Key::Escape, false});
            CHECK_FALSE(GetConsoleInput()->IsOpen());
            CHECK_FALSE(GetWindow().IsTextInputActive());
            CHECK_FALSE(GetInputState().IsKeyPressed(Spot::Key::Escape));
            CHECK(scene->keys == 0);
            DispatchEvent(Spot::KeyPressedEvent{Spot::Key::W, false});
            CHECK(scene->keys == 1);
            CHECK(GetInputState().IsKeyDown(Spot::Key::W));
            Quit();
        }
    } application;
    CHECK(application.Run() == EXIT_SUCCESS);
}
