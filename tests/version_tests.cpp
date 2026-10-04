#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <format>

TEST_CASE("GetVersion matches the project version", "[version]") {
    const Spot::Version expected(SPOT_EXPECTED_VERSION_MAJOR, SPOT_EXPECTED_VERSION_MINOR, SPOT_EXPECTED_VERSION_PATCH);

    CHECK(Spot::GetVersion() == expected);
}

TEST_CASE("GetVersionString matches the project version", "[version]") {
    CHECK(Spot::GetVersionString() == SPOT_EXPECTED_VERSION_STRING);
}

TEST_CASE("GetVersionString is formatted as major.minor.patch", "[version]") {
    const auto version = Spot::GetVersion();
    const auto expected = std::format("{}.{}.{}", version.GetMajor(), version.GetMinor(), version.GetPatch());

    CHECK(Spot::GetVersionString() == expected);
}

TEST_CASE("Versions are ordered by major, then minor, then patch", "[version]") {
    CHECK(Spot::Version(1, 0, 0) > Spot::Version(0, 9, 9));
    CHECK(Spot::Version(0, 2, 0) > Spot::Version(0, 1, 9));
    CHECK(Spot::Version(0, 1, 1) > Spot::Version(0, 1, 0));
    CHECK(Spot::Version(0, 1, 0) == Spot::Version(0, 1, 0));
}
