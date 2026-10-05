#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {

template <typename... Handlers>
struct Overloaded : Handlers... {
    using Handlers::operator()...;
};

} // namespace

TEST_CASE("Event reports the type it was created from", "[event]") {
    const Spot::Event event = Spot::KeyPressedEvent{Spot::Key::Space, true};

    CHECK(event.Is<Spot::KeyPressedEvent>());
    CHECK_FALSE(event.Is<Spot::KeyReleasedEvent>());
}

TEST_CASE("Event gives access to its data only through its own type", "[event]") {
    const Spot::Event event = Spot::WindowResizeEvent{800, 600};

    const auto* resize = event.GetIf<Spot::WindowResizeEvent>();
    REQUIRE(resize != nullptr);
    CHECK(resize->width == 800);
    CHECK(resize->height == 600);
    CHECK(event.GetIf<Spot::WindowCloseEvent>() == nullptr);
}

TEST_CASE("Event visits the handler for its type", "[event]") {
    const Spot::Event event = Spot::MouseButtonPressedEvent{Spot::MouseButton::Right};

    const auto handled = event.Visit(Overloaded{
        [](const Spot::MouseButtonPressedEvent& e) { return e.button == Spot::MouseButton::Right; },
        [](const auto&) { return false; },
    });

    CHECK(handled);
}

TEST_CASE("Keys and mouse buttons have readable names", "[event]") {
    CHECK(Spot::GetKeyName(Spot::Key::Unknown) == "Unknown");
    CHECK(Spot::GetKeyName(Spot::Key::W) == "W");
    CHECK(Spot::GetKeyName(Spot::Key::Digit1) == "Digit1");
    CHECK(Spot::GetKeyName(Spot::Key::Escape) == "Escape");
    CHECK(Spot::GetKeyName(Spot::Key::RightSuper) == "RightSuper");
    CHECK(Spot::GetMouseButtonName(Spot::MouseButton::Left) == "Left");
    CHECK(Spot::GetMouseButtonName(Spot::MouseButton::X2) == "X2");
}
