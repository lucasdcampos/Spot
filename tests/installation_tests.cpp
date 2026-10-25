// Installed tools find the engine they were installed with by where they are, and what their installer wrote.
#include "temp_directory.h"

#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace {

void WriteFile(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

} // namespace

TEST_CASE("An installation is a folder with the engine's package, and what its installer wrote", "[install]") {
    const TempDirectory temp;
    CHECK_FALSE(Spot::Installation::Find(temp.GetPath()));

    WriteFile(temp.GetPath() / "lib" / "cmake" / "Spot" / "SpotConfig.cmake", "");
    const auto bare = Spot::Installation::Find(temp.GetPath());
    REQUIRE(bare);
    CHECK(bare->channel.empty());
    CHECK(bare->source.empty());
    CHECK(bare->GetPackageDirectory() == temp.GetPath() / "lib" / "cmake" / "Spot");
    CHECK(bare->GetEngineLibrary().parent_path() == temp.GetPath() / "bin");
    CHECK(bare->GetPlayer().parent_path() == temp.GetPath() / "player");
    CHECK_FALSE(bare->GetEditor());

    // Written by PowerShell on Windows, with its line ends.
    WriteFile(temp.GetPath() / Spot::Installation::InfoFileName, "channel=nightly\r\nsource=download\r\nother=1\r\n");
    const auto installed = Spot::Installation::Find(temp.GetPath());
    REQUIRE(installed);
    CHECK(installed->channel == "nightly");
    CHECK(installed->source == "download");
}

TEST_CASE("Installed tools build games against their installation, with the toolchain that built them if here",
          "[install]") {
    // These tests run from the engine's build, which is no installation.
    CHECK_FALSE(Spot::Installation::GetCurrent());
    CHECK(Spot::Toolchain::GetCurrent().GetKey() == Spot::Toolchain::GetBuilt().GetKey());

    const TempDirectory temp;
    WriteFile(temp.GetPath() / "lib" / "cmake" / "Spot" / "SpotConfig.cmake", "");
    const auto installation = Spot::Installation::Find(temp.GetPath());
    REQUIRE(installation);
    const Spot::Toolchain built = Spot::Toolchain::GetBuilt();
    const Spot::Toolchain installed = Spot::Toolchain::GetInstalled(*installation);
    CHECK(installed.package == installation->GetPackageDirectory());
    CHECK(installed.engineLibrary == installation->GetEngineLibrary());
    CHECK(installed.player == installation->GetPlayer());
    // What built the tests is on this machine.
    REQUIRE(built.IsAvailable());
    CHECK(installed.cmake == built.cmake);
    CHECK(installed.generator == built.generator);
    CHECK(installed.configuration == built.configuration);
    CHECK(installed.GetKey() != built.GetKey());
}
