#include <spot.h>

#include <catch2/catch_test_macros.hpp>

using Spot::Key;
using Spot::MouseButton;

TEST_CASE("A pressed key is down, and only pressed during its first frame", "[input]") {
    Spot::InputState input;

    input.BeginFrame();
    input.OnEvent(Spot::KeyPressedEvent{Key::W, false});
    CHECK(input.IsKeyDown(Key::W));
    CHECK(input.IsKeyPressed(Key::W));
    CHECK_FALSE(input.IsKeyDown(Key::S));

    input.BeginFrame();
    CHECK(input.IsKeyDown(Key::W));
    CHECK_FALSE(input.IsKeyPressed(Key::W));
}

TEST_CASE("A released key is up, and only released during its first frame", "[input]") {
    Spot::InputState input;
    input.OnEvent(Spot::KeyPressedEvent{Key::W, false});

    input.BeginFrame();
    input.OnEvent(Spot::KeyReleasedEvent{Key::W});
    CHECK_FALSE(input.IsKeyDown(Key::W));
    CHECK(input.IsKeyReleased(Key::W));

    input.BeginFrame();
    CHECK_FALSE(input.IsKeyReleased(Key::W));
}

TEST_CASE("Key repeats do not count as new presses", "[input]") {
    Spot::InputState input;
    input.OnEvent(Spot::KeyPressedEvent{Key::A, false});

    input.BeginFrame();
    input.OnEvent(Spot::KeyPressedEvent{Key::A, true});
    CHECK(input.IsKeyDown(Key::A));
    CHECK_FALSE(input.IsKeyPressed(Key::A));
}

TEST_CASE("A key tapped within one frame is seen as pressed and released", "[input]") {
    Spot::InputState input;

    input.BeginFrame();
    input.OnEvent(Spot::KeyPressedEvent{Key::Space, false});
    input.OnEvent(Spot::KeyReleasedEvent{Key::Space});

    CHECK(input.IsKeyPressed(Key::Space));
    CHECK(input.IsKeyReleased(Key::Space));
    CHECK_FALSE(input.IsKeyDown(Key::Space));
}

TEST_CASE("Mouse buttons follow the same frame rules as keys", "[input]") {
    Spot::InputState input;

    input.BeginFrame();
    input.OnEvent(Spot::MouseButtonPressedEvent{MouseButton::Left});
    CHECK(input.IsMouseButtonDown(MouseButton::Left));
    CHECK(input.IsMouseButtonPressed(MouseButton::Left));
    CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Right));

    input.BeginFrame();
    input.OnEvent(Spot::MouseButtonReleasedEvent{MouseButton::Left});
    CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Left));
    CHECK_FALSE(input.IsMouseButtonPressed(MouseButton::Left));
    CHECK(input.IsMouseButtonReleased(MouseButton::Left));
}

TEST_CASE("Mouse motion keeps the last position and sums the deltas of the frame", "[input]") {
    Spot::InputState input;

    input.BeginFrame();
    input.OnEvent(Spot::MouseMovedEvent{{10.0f, 20.0f}, {2.0f, -1.0f}});
    input.OnEvent(Spot::MouseMovedEvent{{13.0f, 18.0f}, {3.0f, -2.0f}});
    input.OnEvent(Spot::MouseScrolledEvent{{0.0f, 1.0f}});
    input.OnEvent(Spot::MouseScrolledEvent{{0.5f, 1.0f}});

    CHECK(input.GetMousePosition() == Spot::Vector2{13.0f, 18.0f});
    CHECK(input.GetMouseDelta() == Spot::Vector2{5.0f, -3.0f});
    CHECK(input.GetScrollDelta() == Spot::Vector2{0.5f, 2.0f});

    input.BeginFrame();
    CHECK(input.GetMousePosition() == Spot::Vector2{13.0f, 18.0f});
    CHECK(input.GetMouseDelta() == Spot::Vector2{});
    CHECK(input.GetScrollDelta() == Spot::Vector2{});
}

TEST_CASE("Losing focus releases everything that was held", "[input]") {
    Spot::InputState input;
    input.OnEvent(Spot::KeyPressedEvent{Key::LeftShift, false});
    input.OnEvent(Spot::MouseButtonPressedEvent{MouseButton::Right});

    input.BeginFrame();
    input.OnEvent(Spot::WindowFocusEvent{false});

    CHECK_FALSE(input.IsKeyDown(Key::LeftShift));
    CHECK(input.IsKeyReleased(Key::LeftShift));
    CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Right));
    CHECK(input.IsMouseButtonReleased(MouseButton::Right));
}

TEST_CASE("Input reads the state it points at, and nothing without one", "[input]") {
    Spot::InputState state;
    state.OnEvent(Spot::KeyPressedEvent{Key::Escape, false});
    state.OnEvent(Spot::MouseMovedEvent{{4.0f, 2.0f}, {}});

    CHECK_FALSE(Spot::Input::IsKeyDown(Key::Escape));

    Spot::Input::SetState(&state);
    CHECK(Spot::Input::IsKeyDown(Key::Escape));
    CHECK(Spot::Input::IsKeyPressed(Key::Escape));
    CHECK(Spot::Input::GetMousePosition() == Spot::Vector2{4.0f, 2.0f});
    Spot::Input::SetState(nullptr);

    CHECK_FALSE(Spot::Input::IsKeyDown(Key::Escape));
}

TEST_CASE("The cursor stays locked as the game asked, whatever the events", "[input]") {
    Spot::InputState state;
    CHECK_FALSE(state.IsCursorLocked());

    // Without a state, asking does nothing.
    Spot::Input::SetCursorLocked(true);
    CHECK_FALSE(Spot::Input::IsCursorLocked());

    Spot::Input::SetState(&state);
    Spot::Input::SetCursorLocked(true);
    CHECK(state.IsCursorLocked());
    state.BeginFrame();
    state.OnEvent(Spot::WindowFocusEvent{.focused = false});
    CHECK(Spot::Input::IsCursorLocked());
    Spot::Input::SetCursorLocked(false);
    CHECK_FALSE(state.IsCursorLocked());
    Spot::Input::SetState(nullptr);
}

TEST_CASE("Gamepads are tracked one by one, and queried one at a time or all at once", "[input][gamepad]") {
    Spot::InputState state;
    constexpr auto First = static_cast<Spot::GamepadId>(7);
    constexpr auto Second = static_cast<Spot::GamepadId>(9);
    state.OnEvent(Spot::GamepadConnectedEvent{.gamepad = First, .name = "Pad One"});
    state.OnEvent(Spot::GamepadConnectedEvent{.gamepad = Second, .name = "Pad Two"});
    REQUIRE(state.GetGamepadCount() == 2);
    CHECK(state.GetGamepadId(0) == First);
    CHECK(state.GetGamepadName(1) == "Pad Two");
    CHECK(state.GetGamepadId(2) == Spot::GamepadId::Invalid);

    state.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = Second, .button = Spot::GamepadButton::South});
    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = First, .axis = Spot::GamepadAxis::LeftY, .value = 0.5f});
    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Second, .axis = Spot::GamepadAxis::LeftY, .value = -0.8f});
    CHECK(state.IsGamepadButtonDown(Spot::GamepadButton::South));
    CHECK(state.IsGamepadButtonPressed(Spot::GamepadButton::South, 1));
    CHECK_FALSE(state.IsGamepadButtonDown(Spot::GamepadButton::South, 0));
    CHECK(state.GetGamepadAxis(Spot::GamepadAxis::LeftY, 0) == 0.5f);
    // Any gamepad gives the axis farthest from rest.
    CHECK(state.GetGamepadAxis(Spot::GamepadAxis::LeftY) == -0.8f);

    state.BeginFrame();
    CHECK(state.IsGamepadButtonDown(Spot::GamepadButton::South));
    CHECK_FALSE(state.IsGamepadButtonPressed(Spot::GamepadButton::South));
    state.OnEvent(Spot::GamepadButtonReleasedEvent{.gamepad = Second, .button = Spot::GamepadButton::South});
    CHECK(state.IsGamepadButtonReleased(Spot::GamepadButton::South, 1));

    // Losing focus lets go of everything, as for keys.
    state.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = First, .button = Spot::GamepadButton::Start});
    state.OnEvent(Spot::WindowFocusEvent{.focused = false});
    CHECK_FALSE(state.IsGamepadButtonDown(Spot::GamepadButton::Start));
    CHECK(state.GetGamepadAxis(Spot::GamepadAxis::LeftY) == 0.0f);

    state.OnEvent(Spot::GamepadDisconnectedEvent{.gamepad = First});
    REQUIRE(state.GetGamepadCount() == 1);
    CHECK(state.GetGamepadId(0) == Second);

    // A gamepad whose events come before it was said to connect is tracked all the same.
    state.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = First, .button = Spot::GamepadButton::North});
    CHECK(state.GetGamepadCount() == 2);
    CHECK(state.IsGamepadButtonDown(Spot::GamepadButton::North, 1));
    CHECK(Spot::GetGamepadButtonName(Spot::GamepadButton::LeftShoulder) == "Left Shoulder");
    CHECK(Spot::GetGamepadAxisName(Spot::GamepadAxis::RightTrigger) == "Right Trigger");
}

TEST_CASE("Rumbling needs a connected gamepad", "[input][gamepad]") {
    Spot::InputState state;
    Spot::InputState* previous = Spot::Input::GetState();
    Spot::Input::SetState(&state);
    CHECK_FALSE(Spot::Input::RumbleGamepad(1.0f, 1.0f, 0.1f));
    state.OnEvent(Spot::GamepadConnectedEvent{.gamepad = static_cast<Spot::GamepadId>(12345), .name = "Missing"});
    CHECK(Spot::Input::GetGamepadCount() == 1);
    // A gamepad the platform never opened has no motors to shake.
    CHECK_FALSE(Spot::Input::RumbleGamepad(1.0f, 1.0f, 0.1f));
    Spot::Input::SetState(previous);
}

TEST_CASE("A capture withholds input from the game, and releases what was held", "[input][capture]") {
    Spot::InputState input;
    input.OnEvent(Spot::KeyPressedEvent{Key::W, false});
    input.BeginFrame();

    const Spot::InputCaptureId console = input.PushCapture("Console");
    CHECK(input.IsCaptured());
    CHECK(input.GetCaptureOwner() == "Console");
    CHECK_FALSE(input.IsKeyDown(Key::W));
    CHECK(input.IsKeyReleased(Key::W));

    input.BeginFrame();
    input.OnEvent(Spot::KeyPressedEvent{Key::A, false});
    input.OnEvent(Spot::MouseButtonPressedEvent{MouseButton::Left});
    input.OnEvent(Spot::TextInputEvent{"typed"});
    input.OnEvent(Spot::MouseMovedEvent{.position = {5.0f, 6.0f}, .delta = {3.0f, 4.0f}});
    CHECK_FALSE(input.IsKeyDown(Key::A));
    CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Left));
    CHECK(input.GetTypedText().empty());
    CHECK(input.GetMouseDelta() == Spot::Vector2{});
    // Where the cursor is stays known.
    CHECK(input.GetMousePosition() == Spot::Vector2{5.0f, 6.0f});

    input.PopCapture(console);
    CHECK_FALSE(input.IsCaptured());
    input.BeginFrame();
    input.OnEvent(Spot::KeyPressedEvent{Key::D, false});
    CHECK(input.IsKeyDown(Key::D));
}

TEST_CASE("Captures keep the game's cursor lock, applied again when the last one ends", "[input][capture]") {
    Spot::InputState input;
    input.SetCursorLocked(true);
    CHECK(input.IsCursorLockApplied());

    const Spot::InputCaptureId editor = input.PushCapture("Editor");
    const Spot::InputCaptureId field = input.PushCapture("Text Field");
    CHECK(input.IsCursorLocked());
    CHECK_FALSE(input.IsCursorLockApplied());
    CHECK(input.GetCaptureOwner() == "Text Field");

    // In any order.
    input.PopCapture(editor);
    CHECK_FALSE(input.IsCursorLockApplied());
    CHECK(input.GetCaptureOwner() == "Text Field");
    input.PopCapture(editor);
    input.PopCapture(field);
    CHECK(input.IsCursorLockApplied());
    CHECK(input.GetCaptureOwner().empty());
}

TEST_CASE("The mouse delta is zero on each hand-over, until the frame after", "[input][capture]") {
    Spot::InputState input;
    const Spot::InputCaptureId capture = input.PushCapture("Overlay");
    input.BeginFrame();
    input.PopCapture(capture);
    input.OnEvent(Spot::MouseMovedEvent{.position = {}, .delta = {1.0f, 1.0f}});
    CHECK(input.GetMouseDelta() == Spot::Vector2{});

    // The frame after, when the jump of a cursor locked again arrives.
    input.BeginFrame();
    input.OnEvent(Spot::MouseMovedEvent{.position = {}, .delta = {400.0f, 300.0f}});
    CHECK(input.GetMouseDelta() == Spot::Vector2{});

    input.BeginFrame();
    input.OnEvent(Spot::MouseMovedEvent{.position = {}, .delta = {2.0f, 0.0f}});
    CHECK(input.GetMouseDelta() == Spot::Vector2{2.0f, 0.0f});

    // Locking the cursor is a hand-over too.
    input.SetCursorLocked(true);
    CHECK(input.GetMouseDelta() == Spot::Vector2{});
}

TEST_CASE("Input's captures reach the current state", "[input][capture]") {
    CHECK(Spot::Input::PushCapture("Nobody") == Spot::InputCaptureId::None);
    Spot::InputState input;
    Spot::Input::SetState(&input);
    const Spot::InputCaptureId capture = Spot::Input::PushCapture("Console");
    CHECK(Spot::Input::IsCaptured());
    Spot::Input::PopCapture(capture);
    CHECK_FALSE(Spot::Input::IsCaptured());
    Spot::Input::SetState(nullptr);
}
