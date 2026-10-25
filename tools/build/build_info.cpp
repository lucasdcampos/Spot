// The only file that knows how the tools were built, so a change to that rebuilds nothing else.
#include "build/code_build.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <string_view>
#include <system_error>
#include <vector>

namespace Spot {

namespace {

#if defined(_WIN32)
constexpr char PathSeparator = ';';
constexpr std::string_view ExecutableSuffix = ".exe";
#else
constexpr char PathSeparator = ':';
constexpr std::string_view ExecutableSuffix = "";
#endif

// Where programs are installed without being on the PATH, as Visual Studio's CMake, or that applications opened from
// the desktop do not have on theirs, as on macOS.
std::vector<std::filesystem::path> GetOtherProgramDirectories() {
    std::vector<std::filesystem::path> directories;
#if defined(_WIN32)
    if (const auto programFiles = Process::GetEnvironmentValue("ProgramFiles")) {
        const std::filesystem::path root = Utf8ToPath(*programFiles);
        directories.push_back(root / "CMake" / "bin");
        // Visual Studio/<version>/<edition>, the newest first.
        std::vector<std::filesystem::path> editions;
        std::error_code error;
        for (const auto& version : std::filesystem::directory_iterator(root / "Microsoft Visual Studio", error)) {
            for (const auto& edition : std::filesystem::directory_iterator(version.path(), error)) {
                editions.push_back(edition.path());
            }
        }
        std::ranges::sort(editions, std::greater());
        for (const auto& edition : editions) {
            directories.push_back(edition / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" /
                                  "bin");
        }
    }
#elif defined(__APPLE__)
    directories = {"/opt/homebrew/bin", "/usr/local/bin", "/Applications/CMake.app/Contents/bin"};
#else
    directories = {"/usr/local/bin", "/usr/bin"};
#endif
    return directories;
}

std::string FindProgram(std::string_view name) {
    std::vector<std::filesystem::path> directories;
    const std::string path = Process::GetEnvironmentValue("PATH").value_or("");
    for (std::size_t start = 0; start <= path.size();) {
        const std::size_t end = std::min(path.find(PathSeparator, start), path.size());
        if (end > start) {
            directories.push_back(Utf8ToPath(std::string_view(path).substr(start, end - start)));
        }
        start = end + 1;
    }
    std::ranges::copy(GetOtherProgramDirectories(), std::back_inserter(directories));
    for (const auto& directory : directories) {
        const std::filesystem::path program = directory / Utf8ToPath(std::string(name) + std::string(ExecutableSuffix));
        std::error_code error;
        if (std::filesystem::is_regular_file(program, error)) {
            return PathToUtf8(program);
        }
    }
    return {};
}

} // namespace

Toolchain Toolchain::GetBuilt() {
    return {
        .cmake = SPOT_TOOLCHAIN_CMAKE,
        .generator = SPOT_TOOLCHAIN_GENERATOR,
        .platform = SPOT_TOOLCHAIN_PLATFORM,
        .toolset = SPOT_TOOLCHAIN_TOOLSET,
        .instance = SPOT_TOOLCHAIN_INSTANCE,
        .compiler = SPOT_TOOLCHAIN_COMPILER,
        .makeProgram = SPOT_TOOLCHAIN_MAKE_PROGRAM,
        .configuration = SPOT_TOOLCHAIN_CONFIGURATION,
        .multiConfiguration = SPOT_TOOLCHAIN_MULTI_CONFIGURATION != 0,
        .package = Utf8ToPath(SPOT_TOOLCHAIN_PACKAGE),
        .engineLibrary = Utf8ToPath(SPOT_TOOLCHAIN_ENGINE_LIBRARY),
        .player = Utf8ToPath(SPOT_TOOLCHAIN_PLAYER),
    };
}

Toolchain Toolchain::GetInstalled(const Installation& installation) {
    Toolchain toolchain = GetBuilt();
    toolchain.package = installation.GetPackageDirectory();
    toolchain.engineLibrary = installation.GetEngineLibrary();
    toolchain.player = installation.GetPlayer();
    if (toolchain.IsAvailable()) {
        return toolchain;
    }
    // Built on another machine. CMake picks the compiler, which must build code that works with the engine's: the one
    // of Visual Studio on Windows, with its default generator, or the system's elsewhere, with Ninja when there is
    // one, as it is faster than Make. The configuration stays the engine's, as code built in another one does not
    // work with it on Windows.
    toolchain.cmake = FindProgram("cmake");
    toolchain.generator.clear();
    toolchain.platform.clear();
    toolchain.toolset.clear();
    toolchain.instance.clear();
    toolchain.compiler.clear();
    toolchain.makeProgram.clear();
    toolchain.multiConfiguration = false;
#if !defined(_WIN32)
    toolchain.makeProgram = FindProgram("ninja");
    if (!toolchain.makeProgram.empty()) {
        toolchain.generator = "Ninja";
    }
#endif
    return toolchain;
}

Toolchain Toolchain::GetCurrent() {
    const auto installation = Installation::GetCurrent();
    return installation ? GetInstalled(*installation) : GetBuilt();
}

} // namespace Spot
