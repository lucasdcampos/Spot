#include <spot.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Window is created with the requested title and size", "[window]") {
    const auto window = Spot::Window::Create({.title = "Spot Tests", .width = 320, .height = 240});

    REQUIRE(window != nullptr);
    CHECK(window->GetTitle() == "Spot Tests");
    CHECK(window->GetWidth() == 320);
    CHECK(window->GetHeight() == 240);
}

TEST_CASE("Window title can be changed", "[window]") {
    const auto window = Spot::Window::Create({.title = "Before"});
    REQUIRE(window != nullptr);

    window->SetTitle("After");

    CHECK(window->GetTitle() == "After");
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
