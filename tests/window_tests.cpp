#include <spot.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Window is created with the requested title and size", "[window]") {
    const auto window = Spot::Window::Create({.title = "Spot Tests", .width = 320, .height = 240});

    REQUIRE(window != nullptr);
    CHECK(window->GetTitle() == "Spot Tests");
    CHECK(window->GetWidth() == 320);
    CHECK(window->GetHeight() == 240);
    CHECK(window->GetPixelWidth() >= 320);
    CHECK(window->GetPixelHeight() >= 240);
    CHECK(window->GetNativeHandle() != nullptr);
}

TEST_CASE("Window title can be changed", "[window]") {
    const auto window = Spot::Window::Create({.title = "Before"});
    REQUIRE(window != nullptr);

    window->SetTitle("After");

    CHECK(window->GetTitle() == "After");
}

TEST_CASE("Window captures and releases the mouse", "[window]") {
    const auto window = Spot::Window::Create();
    REQUIRE(window != nullptr);
    CHECK_FALSE(window->IsMouseCaptured());

    // Headless video drivers may not support capturing, but must report it.
    const bool captured = window->SetMouseCaptured(true);
    CHECK(window->IsMouseCaptured() == captured);

    CHECK(window->SetMouseCaptured(false));
    CHECK_FALSE(window->IsMouseCaptured());
}

TEST_CASE("Several windows can be created and destroyed in any order", "[window]") {
    auto first = Spot::Window::Create();
    auto second = Spot::Window::Create();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    first.reset();
    CHECK(second->GetWidth() > 0);
    second.reset();

    CHECK(Spot::Window::Create() != nullptr);
}

TEST_CASE("Window can be resized and reports its content scale", "[window]") {
    const auto window = Spot::Window::Create({.width = 320, .height = 240});
    REQUIRE(window != nullptr);

    window->SetSize(400, 300);

    CHECK(window->GetWidth() == 400);
    CHECK(window->GetHeight() == 300);
    CHECK(window->GetContentScale() > 0.0f);
    CHECK_FALSE(window->IsMinimized());
}

TEST_CASE("Window turns text input on and off", "[window]") {
    const auto window = Spot::Window::Create();
    REQUIRE(window != nullptr);
    CHECK_FALSE(window->IsTextInputActive());

    window->StartTextInput();
    window->SetTextInputArea({10.0f, 20.0f}, {100.0f, 16.0f});
    CHECK(window->IsTextInputActive());

    window->StopTextInput();
    CHECK_FALSE(window->IsTextInputActive());
}

TEST_CASE("Window remembers the cursor asked for and shows or hides it", "[window]") {
    const auto window = Spot::Window::Create();
    REQUIRE(window != nullptr);
    CHECK(window->GetCursor() == Spot::Cursor::Arrow);

    window->SetCursor(Spot::Cursor::Text);
    CHECK(window->GetCursor() == Spot::Cursor::Text);

    window->SetCursorVisible(false);
    CHECK_FALSE(window->IsCursorVisible());
    window->SetCursorVisible(true);
    CHECK(window->IsCursorVisible());
}

TEST_CASE("Clipboard text round-trips as UTF-8", "[window]") {
    // The window keeps the platform running, which holds the clipboard of headless drivers.
    const auto window = Spot::Window::Create();
    REQUIRE(window != nullptr);

    REQUIRE(Spot::Clipboard::SetText("Spot éditeur"));

    CHECK(Spot::Clipboard::HasText());
    CHECK(Spot::Clipboard::GetText() == "Spot éditeur");
}
