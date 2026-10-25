// The manual's scripting reference is generated from the script API: it fails here when they differ, and
// SPOT_UPDATE_DOCS=1 writes it again, as SPOT_UPDATE_GOLDEN=1 does golden images.
#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

TEST_CASE("The manual's scripting reference matches the script API", "[tools][docs]") {
    const std::string reference = Spot::MakeScriptReference();
    CHECK(reference.find("### Entity") != std::string::npos);
    CHECK(reference.find("### SpriteRenderer") != std::string::npos);
    CHECK(reference.find("`OnUpdate` | 1") != std::string::npos);

    const std::filesystem::path path = std::filesystem::path(SPOT_DOCS_DIR) / "manual/reference/scripting_api.md";
    const auto update = Spot::Process::GetEnvironmentValue("SPOT_UPDATE_DOCS");
    if (update && *update == "1") {
        std::ofstream(path, std::ios::binary) << reference;
        WARN("Wrote " << path.string());
        return;
    }
    std::ifstream file(path, std::ios::binary);
    const std::string committed{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    INFO("Run the tests with SPOT_UPDATE_DOCS=1 to write " << path.string() << " again");
    CHECK(committed == reference);
}
