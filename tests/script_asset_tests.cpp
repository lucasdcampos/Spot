#include "log_capture.h"
#include "script_asset_fixture.h"

#include "assets/asset_file.h"
#include "assets/asset_manifest.h"
#include "assets/asset_pak.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

TEST_CASE("Script assets import vice files with stable metadata and cached source", "[assets][script]") {
    ScriptAssetFixture fixture;
    const std::string source = "override func OnStart() { print(\"hello\", null) }\n";
    CHECK(Spot::GetSourceAssetType("scripts/hello.VICE") == Spot::AssetType::Script);
    CHECK(Spot::GetSourceAssetType("scripts/hello.LUA") == Spot::AssetType::Script);
    CHECK(Spot::ParseAssetType("Script") == Spot::AssetType::Script);
    CHECK(Spot::GetAssetTypeName(Spot::AssetType::Script) == "Script");
    const auto id = fixture.Import("scripts/hello.vice", source);
    const auto luaId = fixture.Import("scripts/hello.lua", "function OnStart() print(\"hello\", nil) end\n");
    REQUIRE(std::filesystem::is_regular_file(fixture.sources / "scripts/hello.vice.meta"));
    REQUIRE(std::filesystem::is_regular_file(fixture.sources / "scripts/hello.lua.meta"));
    REQUIRE(fixture.database.GetMeta(id));
    REQUIRE(fixture.database.GetMeta(luaId));
    CHECK(fixture.database.GetMeta(id)->id == id);
    CHECK(fixture.database.GetMeta(id)->type == Spot::AssetType::Script);
    CHECK(fixture.database.GetMeta(luaId)->id == luaId);
    CHECK(fixture.database.GetMeta(luaId)->type == Spot::AssetType::Script);
    CHECK(fixture.database.Refresh() == 0);

    Spot::AssetManager assets(fixture.imported);
    const auto script = assets.Load<Spot::ScriptSource>("scripts/hello.vice");
    const auto luaScript = assets.Load<Spot::ScriptSource>("scripts/hello.lua");
    REQUIRE(script);
    REQUIRE(luaScript);
    CHECK(script.GetId() == id);
    CHECK(script->source == source);
    CHECK(script->sourceName == "scripts/hello.vice");
    CHECK(luaScript.GetId() == luaId);
    CHECK(luaScript->sourceName == "scripts/hello.lua");
    CHECK(assets.Load<Spot::ScriptSource>(id).Get() == script.Get());
    CHECK(assets.GetAssetIds(Spot::AssetType::Script, "scripts").size() == 2);

    REQUIRE(fixture.database.Move("scripts/hello.vice", "scripts/renamed.vice"));
    fixture.database.Refresh();
    CHECK(fixture.database.FindByPath("scripts/renamed.vice") == id);
    CHECK_FALSE(fixture.database.FindByPath("scripts/hello.vice"));
    assets.RefreshManifest();
    const auto renamed = assets.Load<Spot::ScriptSource>(id);
    REQUIRE(renamed);
    CHECK(renamed->sourceName == "scripts/renamed.vice");
    CHECK(renamed.Get() != script.Get());
}

TEST_CASE("Script assets support empty files UTF8 BOM and source that is parsed later", "[assets][script]") {
    ScriptAssetFixture fixture;
    fixture.Write("empty.vice", "");
    fixture.Write("bom.vice", "\xEF\xBB\xBF// UTF-8\r\nfunc start() { print(\"\xC3\xA9\") }\r\n");
    fixture.Write("unfinished.vice", "override func OnStart() {");
    REQUIRE(fixture.database.Refresh() == 3);
    Spot::AssetManager assets(fixture.imported);
    const auto empty = assets.Load<Spot::ScriptSource>("empty.vice");
    REQUIRE(empty);
    CHECK(empty->source.empty());
    const auto bom = assets.Load<Spot::ScriptSource>("bom.vice");
    REQUIRE(bom);
    CHECK(bom->source == "// UTF-8\r\nfunc start() { print(\"\xC3\xA9\") }\r\n");
    const auto unfinished = assets.Load<Spot::ScriptSource>("unfinished.vice");
    REQUIRE(unfinished);
    CHECK(unfinished->source == "override func OnStart() {");
}

TEST_CASE("Reimporting script assets invalidates the cached source without changing its id", "[assets][script]") {
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("counter.vice", "var n = 1");
    Spot::AssetManager assets(fixture.imported);
    const auto old = assets.Load<Spot::ScriptSource>(id);
    REQUIRE(old);
    fixture.Write("counter.vice", "var n = 2");
    REQUIRE(fixture.database.Refresh() == 1);
    CHECK(fixture.database.FindByPath("counter.vice") == id);
    assets.RefreshManifest();
    assets.Reload(fixture.database.GetChanged());
    const auto updated = assets.Load<Spot::ScriptSource>(id);
    REQUIRE(updated);
    CHECK(updated.Get() != old.Get());
    CHECK(updated->source == "var n = 2");
    CHECK(old->source == "var n = 1");
}

TEST_CASE("Script assets load from a shipped pak without source or loose imported files", "[assets][script]") {
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("scripts/shipped.vice", "override func OnStart() { print(\"shipped\") }");
    const auto shipped = fixture.temp.GetPath() / "shipped";
    std::filesystem::create_directories(shipped);
    std::filesystem::copy_file(Spot::Detail::Manifest::GetPath(fixture.imported),
                               Spot::Detail::Manifest::GetPath(shipped));
    REQUIRE(Spot::Detail::WriteAssetPak(shipped / Spot::Detail::AssetPakFileName, fixture.imported));
    std::filesystem::rename(fixture.sources, fixture.temp.GetPath() / "unavailable_sources");
    Spot::AssetManager assets(shipped);
    const auto script = assets.Load<Spot::ScriptSource>(id);
    REQUIRE(script);
    CHECK(script->source == "override func OnStart() { print(\"shipped\") }");
    CHECK(script->sourceName == "scripts/shipped.vice");
    CHECK_FALSE(std::filesystem::exists(Spot::Detail::GetImportedAssetPath(shipped, id)));
}

TEST_CASE("Script asset loading rejects corrupt payloads", "[assets][script]") {
    auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks({sink});
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("bad.vice", "");
    for (const bool truncated : {true, false}) {
        Spot::Detail::BinaryWriter writer;
        if (truncated) {
            writer.Write(std::uint32_t{50});
        } else {
            writer.WriteString("override func OnStart() {}");
            writer.Write(std::uint8_t{0});
        }
        REQUIRE(Spot::Detail::WriteAssetFile(Spot::Detail::GetImportedAssetPath(fixture.imported, id),
                                             Spot::AssetType::Script, writer.GetData()));
        Spot::AssetManager assets(fixture.imported);
        const auto script = assets.Load<Spot::ScriptSource>(id);
        CHECK_FALSE(script);
        CHECK(script.GetId() == id);
    }
}

TEST_CASE("Script assets participate in asynchronous loading and preloading", "[assets][script]") {
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("async.vice", "var n = null");
    Spot::AssetManager assets(fixture.imported);
    const auto request = assets.LoadAsync<Spot::ScriptSource>(id);
    REQUIRE(request.IsValid());
    CHECK_FALSE(assets.IsLoaded(id));
    for (int i = 0; i < 1000 && !request.IsDone(); ++i) {
        assets.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(request.IsDone());
    REQUIRE(request.Get());
    CHECK(request.Get()->source == "var n = null");
    Spot::AssetManager preloadAssets(fixture.imported);
    const std::array ids{id};
    const auto preloaded = preloadAssets.Preload(ids);
    REQUIRE(preloaded.size() == 1);
    REQUIRE(preloadAssets.Load<Spot::ScriptSource>(id));
    CHECK(preloadAssets.Load<Spot::ScriptSource>(id).Get() == preloaded.front().get());
}
