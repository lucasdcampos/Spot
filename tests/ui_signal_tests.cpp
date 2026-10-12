#include <catch2/catch_test_macros.hpp>
#include <spot.h>
#include <stdexcept>
#include <vector>

TEST_CASE("UI signal snapshots stay independent through nested dispatch and exceptions", "[ui][signals]") {
    Spot::Signal<int> signal;
    std::vector<int> seen;
    auto first = signal.Connect([&](int value) {
        seen.push_back(value);
        if (value == 1)
            signal.Emit(2);
    });
    auto second = signal.Connect([&](int value) { seen.push_back(value + 10); });
    signal.Emit(1);
    CHECK(seen == std::vector<int>{1, 2, 12, 11});
    auto throwing = signal.Connect([](int value) {
        if (value == 4)
            throw std::runtime_error("callback");
    });
    CHECK_THROWS_AS(signal.Emit(4), std::runtime_error);
    seen.clear();
    signal.Emit(3);
    CHECK(seen == std::vector<int>{3, 13});
    second.Disconnect();
    seen.clear();
    signal.Emit(1);
    CHECK(seen == std::vector<int>{1, 2});
}
