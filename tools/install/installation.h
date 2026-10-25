#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Spot {

// An installed engine, as `cmake --install` lays it out (see cmake/spot_install.cmake) and the installers install it
// (scripts/install.ps1, scripts/install.sh): the tools in its bin/, and what games build against beside them. Tools
// run from the engine's build directory are not installed.
struct Installation {
    // What the installers write about the installation, which `spot update` reads.
    static constexpr const char* InfoFileName = "install.txt";

    std::filesystem::path directory;
    // The releases it follows, "stable" or "nightly", and how it got here, "download" or "source"; empty when
    // installed by other means, such as `cmake --install`.
    std::string channel;
    std::string source;

    // The installation in the folder, if there is one.
    static std::optional<Installation> Find(const std::filesystem::path& directory);
    // The one these tools run from, if any.
    static std::optional<Installation> GetCurrent();

    std::filesystem::path GetBinDirectory() const;
    // The engine's CMake package, which games' code builds against: Spot_DIR.
    std::filesystem::path GetPackageDirectory() const;
    std::filesystem::path GetEngineLibrary() const;
    std::filesystem::path GetPlayer() const;
    // The editor, if it was installed.
    std::optional<std::filesystem::path> GetEditor() const;
};

} // namespace Spot
