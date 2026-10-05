#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

TEST_CASE("Images load as RGBA with rows from top to bottom", "[image]") {
    const auto image = Spot::Image::Load(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "pixels.png");

    REQUIRE(image.has_value());
    CHECK(image->GetWidth() == 2);
    CHECK(image->GetHeight() == 2);
    const std::vector<std::uint8_t> expected{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0};
    CHECK(std::vector<std::uint8_t>(image->GetPixels().begin(), image->GetPixels().end()) == expected);
}

TEST_CASE("Images that cannot be read or decoded are reported", "[image]") {
    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});

    CHECK_FALSE(Spot::Image::Load(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "missing.png").has_value());
    const std::array<std::byte, 4> garbage{};
    CHECK_FALSE(Spot::Image::LoadFromMemory(garbage).has_value());
    CHECK(capture->GetRecords().size() == 2);
}

TEST_CASE("Images can be built from pixels", "[image]") {
    const Spot::Image image(1, 1, {10, 20, 30, 40});

    CHECK(image.GetWidth() == 1);
    CHECK(image.GetPixels()[3] == 40);
}

TEST_CASE("The executable directory holds the running executable", "[paths]") {
    const auto directory = Spot::GetExecutableDirectory();

    CHECK(std::filesystem::is_directory(directory));
    CHECK((std::filesystem::exists(directory / "spot_tests") || std::filesystem::exists(directory / "spot_tests.exe")));
}

TEST_CASE("Paths convert to UTF-8 text, including non-ASCII names", "[paths]") {
    const std::filesystem::path path(u8"assets/ação.png");

    CHECK(Spot::PathToUtf8(path) == "assets/a\xc3\xa7\xc3\xa3o.png");
}
