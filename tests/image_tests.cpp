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

TEST_CASE("Debug images are opaque grays with their patterns", "[image]") {
    const auto gray = [](const Spot::Image& image, std::uint32_t x, std::uint32_t y) {
        const auto* pixel = &image.GetPixels()[(static_cast<std::size_t>(y) * image.GetWidth() + x) * 4];
        CHECK(pixel[0] == pixel[1]);
        CHECK(pixel[1] == pixel[2]);
        CHECK(pixel[3] == 255);
        return pixel[0];
    };

    const auto grid = Spot::Image::CreateGrid(64);
    REQUIRE(grid.GetWidth() == 64);
    REQUIRE(grid.GetHeight() == 64);
    const auto border = gray(grid, 0, 30);
    const auto quarterLine = gray(grid, 16, 30);
    const auto inside = gray(grid, 30, 30);
    CHECK(border < quarterLine);
    CHECK(quarterLine < inside);
    // Only two edges have a border, so repeated squares do not double it.
    CHECK(gray(grid, 63, 30) == inside);

    const auto checker = Spot::Image::CreateChecker(64);
    REQUIRE(checker.GetWidth() == 64);
    CHECK(gray(checker, 0, 0) != gray(checker, 8, 0));
    CHECK(gray(checker, 0, 0) == gray(checker, 8, 8));
    CHECK(gray(checker, 7, 7) == gray(checker, 0, 0));
}

TEST_CASE("The executable directory holds the running executable", "[paths]") {
    const auto directory = Spot::GetExecutableDirectory();

    CHECK(std::filesystem::is_directory(directory));
    CHECK((std::filesystem::exists(directory / "spot_tests") || std::filesystem::exists(directory / "spot_tests.exe")));
}

TEST_CASE("The user data directory exists and is named after the application", "[paths]") {
    const auto directory = Spot::GetUserDataDirectory("Spot", "Spot Tests");

    REQUIRE_FALSE(directory.empty());
    CHECK(std::filesystem::is_directory(directory));
    CHECK(directory.filename() == "Spot Tests");
}

TEST_CASE("Paths convert to UTF-8 text, including non-ASCII names", "[paths]") {
    const std::filesystem::path path(u8"assets/ação.png");

    CHECK(Spot::PathToUtf8(path) == "assets/a\xc3\xa7\xc3\xa3o.png");
}
