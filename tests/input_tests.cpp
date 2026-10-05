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
