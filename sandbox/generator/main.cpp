// sandbox_generator: writes the sandbox's generated assets (see generator.h), into sandbox/assets/generated or the
// folder given.
#include "generator.h"

#include <spot.h>

#include <cstdlib>
#include <filesystem>

int main(int argc, char** argv) {
    const std::filesystem::path directory = argc > 1 ? Spot::Utf8ToPath(argv[1]) : SANDBOX_GENERATED_DIRECTORY;
    const Generator::OutputCounts counts = Generator::GenerateSandboxAssets(directory);
    Spot::Log::Info("{} files written, {} unchanged, {} failed, in '{}'", counts.written, counts.unchanged,
                    counts.failed, Spot::PathToUtf8(std::filesystem::absolute(directory)));
    return counts.failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
