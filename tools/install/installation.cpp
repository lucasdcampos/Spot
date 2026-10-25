#include "install/installation.h"

#include <spot.h>

#include <fstream>
#include <string_view>
#include <system_error>

namespace Spot {

std::optional<Installation> Installation::Find(const std::filesystem::path& directory) {
    Installation installation{.directory = directory};
    std::error_code error;
    if (!std::filesystem::is_regular_file(installation.GetPackageDirectory() / "SpotConfig.cmake", error)) {
        return std::nullopt;
    }
    // Lines of "name=value".
    std::ifstream info(directory / InfoFileName);
    for (std::string line; std::getline(info, line);) {
        if (line.ends_with('\r')) {
            line.pop_back();
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string_view name = std::string_view(line).substr(0, equals);
        std::string value = line.substr(equals + 1);
        if (name == "channel") {
            installation.channel = std::move(value);
        } else if (name == "source") {
            installation.source = std::move(value);
        }
    }
    return installation;
}

std::optional<Installation> Installation::GetCurrent() {
    // The executable's folder may end in a separator, which parent_path() takes for an empty last part.
    std::filesystem::path bin = GetExecutableDirectory();
    if (!bin.has_filename()) {
        bin = bin.parent_path();
    }
    return Find(bin.parent_path());
}

std::filesystem::path Installation::GetBinDirectory() const {
    return directory / "bin";
}

std::filesystem::path Installation::GetPackageDirectory() const {
    return directory / "lib" / "cmake" / "Spot";
}

std::filesystem::path Installation::GetEngineLibrary() const {
    return GetBinDirectory() / SPOT_INSTALLED_ENGINE_LIBRARY;
}

std::filesystem::path Installation::GetPlayer() const {
    return directory / "player" / SPOT_INSTALLED_PLAYER;
}

std::optional<std::filesystem::path> Installation::GetEditor() const {
    const std::filesystem::path editor = GetBinDirectory() / SPOT_INSTALLED_EDITOR;
    std::error_code error;
    return std::filesystem::is_regular_file(editor, error) ? std::optional(editor) : std::nullopt;
}

} // namespace Spot
