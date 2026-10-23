#include "temp_directory.h"

#include <spot_tools.h>

#include <Luau/Parser.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void Write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

} // namespace

TEST_CASE("The Luau module and definitions describe the script API in syntax Luau parses", "[tools][luau]") {
    const std::string module = Spot::MakeLuauModule();
    const std::string definitions = Spot::MakeLuauDefinitions();
    for (const auto& [text, declarations] : {std::pair{&module, false}, std::pair{&definitions, true}}) {
        Luau::Allocator allocator;
        Luau::AstNameTable names(allocator);
        Luau::ParseOptions options;
        options.allowDeclarationSyntax = declarations;
        const auto result = Luau::Parser::parse(text->data(), text->size(), names, allocator, options);
        for (const auto& error : result.errors)
            FAIL_CHECK(error.getLocation().begin.line + 1 << ": " << error.getMessage());
    }

    CHECK(module.find("export type Entity = {\n") != std::string::npos);
    CHECK(module.find("    name: string,\n") != std::string::npos);
    CHECK(module.find("    parent: Entity?,\n") != std::string::npos);
    CHECK(module.find("    GetComponent: (self: Entity, typeName: any, index: any?) -> Component?,\n") !=
          std::string::npos);
    CHECK(module.find("    SetName: (self: Entity, value: string) -> (),\n") != std::string::npos);
    CHECK(module.find("    Emit: (self: Entity, name: any, ...any) -> number,\n") != std::string::npos);
    CHECK(module.find("    StartCoroutine: (self: Component, callback: any) -> number,\n") != std::string::npos);
    CHECK(module.find("    IsKeyDown: (self: Input, key: any) -> boolean,\n") != std::string::npos);
    CHECK(module.find("    [string]: any,\n") != std::string::npos);
    CHECK(module.find("export type SpotModule = {\n") != std::string::npos);
    CHECK(module.find("    entity: Entity,\n") != std::string::npos);
    CHECK(module.find("export type Vector3 = {\n") != std::string::npos);
    CHECK(module.ends_with("return (nil :: any) :: SpotModule\n"));
    CHECK(definitions.find("declare function Vector3(x: number, y: number, z: number): { x: number, y: number, z: "
                           "number }\n") != std::string::npos);
    CHECK(definitions.find("declare function Color(r: number, g: number, b: number, a: number)") != std::string::npos);
}

TEST_CASE("Luau tooling points projects' .luaurc at the module and keeps their own settings", "[tools][luau]") {
    TempDirectory temp;
    auto project = Spot::Project::Create(temp.GetPath() / "Game", {.name = "Game"});
    REQUIRE(project);
    const auto root = project->GetDirectory();

    REQUIRE(Spot::WriteLuauTypes(*project));
    CHECK(Read(root / ".spot/luau/spot.luau") == Spot::MakeLuauModule());
    CHECK(Read(root / ".spot/luau/spot.d.luau") == Spot::MakeLuauDefinitions());
    CHECK_FALSE(std::filesystem::exists(root / ".luaurc"));

    REQUIRE(Spot::WriteLuauTooling(*project));
    CHECK(nlohmann::json::parse(Read(root / ".luaurc")) == nlohmann::json{{"languageMode", "nonstrict"},
                                                                          {"lint", {{"FunctionUnused", false}}},
                                                                          {"aliases", {{"spot", ".spot/luau/spot"}}}});
    CHECK_FALSE(std::filesystem::exists(root / ".vscode"));

    Write(root / ".luaurc", R"({"languageMode": "strict", "aliases": {"shared": "../shared"}})");
    REQUIRE(Spot::WriteLuauTooling(*project));
    const auto config = nlohmann::json::parse(Read(root / ".luaurc"));
    CHECK(config["languageMode"] == "strict");
    CHECK_FALSE(config.contains("lint"));
    CHECK(config["aliases"]["shared"] == "../shared");
    CHECK(config["aliases"]["spot"] == ".spot/luau/spot");
    const auto written = std::filesystem::last_write_time(root / ".luaurc");
    REQUIRE(Spot::WriteLuauTooling(*project));
    CHECK(std::filesystem::last_write_time(root / ".luaurc") == written);

    const std::string commented = "{\n    // Mine\n    \"aliases\": {}\n}\n";
    Write(root / ".luaurc", commented);
    REQUIRE(Spot::WriteLuauTooling(*project));
    CHECK(Read(root / ".luaurc") == commented);
}
