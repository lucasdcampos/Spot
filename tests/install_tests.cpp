// The engine as `cmake --install` lays it out, which releases ship and the installers install: moved elsewhere, its
// tools make games with and without code against it.
#include "temp_directory.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Result {
    int exitCode = -1;
    std::vector<std::string> output;

    bool Says(std::string_view text) const {
        return std::ranges::any_of(output, [&](const std::string& line) { return line.find(text) != line.npos; });
    }
};

Result Run(std::vector<std::string> command) {
    const auto process = Spot::Process::Start(command);
    REQUIRE(process);
    Result result;
    result.exitCode = process->Wait();
    result.output = process->TakeOutput();
    if (result.exitCode != 0) {
        for (const std::string& line : result.output) {
            UNSCOPED_INFO(line);
        }
    }
    return result;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("An installed engine works wherever it is put, making games with and without code", "[install]") {
    const TempDirectory temp;
    // With a space, as users' folders often have.
    const std::filesystem::path prefix = temp.GetPath() / "Spot Engine";
    REQUIRE(Run({SPOT_CMAKE_COMMAND, "--install", SPOT_BUILD_DIRECTORY, "--config", SPOT_BUILD_CONFIGURATION,
                 "--component", "spot", "--prefix", Spot::PathToUtf8(prefix)})
                .exitCode == 0);
    CHECK(ReadFile(prefix / "version.txt") == std::string(Spot::GetVersionString()) + "\n");
    CHECK(std::filesystem::is_regular_file(prefix / "include" / "spot.h"));
    CHECK(std::filesystem::is_regular_file(prefix / "share" / "spot" / "shaders" / "surface_lighting.hlsl"));
    // Games never see the importers or the backends.
    CHECK_FALSE(std::filesystem::exists(prefix / "include" / "spot_import.h"));
    CHECK_FALSE(std::filesystem::exists(prefix / "include" / "spot" / "import"));
    CHECK_FALSE(std::filesystem::exists(prefix / "include" / "spot" / "graphics" / "nvrhi"));

    // As an archive extracted anywhere.
    const std::filesystem::path moved = temp.GetPath() / "Moved";
    std::filesystem::rename(prefix, moved);
    const std::string spot = Spot::PathToUtf8(moved / "bin" / ("spot" SPOT_EXECUTABLE_SUFFIX));
    const Result version = Run({spot, "version"});
    CHECK(version.exitCode == 0);
    CHECK(version.Says("Installed in " + Spot::PathToUtf8(moved)));
    CHECK(Run({spot, "doctor"}).exitCode == 0);

    const std::string location = Spot::PathToUtf8(temp.GetPath());
    REQUIRE(Run({spot, "new", "Scripted", "--path", location, "--code", "none"}).exitCode == 0);
    const std::filesystem::path exported = temp.GetPath() / "Exported";
    REQUIRE(Run({spot, "export", Spot::PathToUtf8(temp.GetPath() / "Scripted"), "--out", Spot::PathToUtf8(exported)})
                .exitCode == 0);
    CHECK(std::filesystem::is_regular_file(exported / ("Scripted" SPOT_EXECUTABLE_SUFFIX)));
    CHECK(std::filesystem::is_regular_file(exported / SPOT_ENGINE_LIBRARY_NAME));

    // Against the installed package, with its engine library beside the game.
    REQUIRE(Run({spot, "new", "Coded", "--path", location}).exitCode == 0);
    REQUIRE(Run({spot, "build", Spot::PathToUtf8(temp.GetPath() / "Coded")}).exitCode == 0);
    const std::filesystem::path game = temp.GetPath() / "Coded" / ".spot" / "build" / "game";
    CHECK(std::filesystem::is_regular_file(game / ("game" SPOT_EXECUTABLE_SUFFIX)));
    CHECK(std::filesystem::is_regular_file(game / SPOT_ENGINE_LIBRARY_NAME));
}
