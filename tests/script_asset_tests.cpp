
#include <spot/assets/script_source.h>

#include "log_capture.h"
#include "registered_assets.h"
#include "script_asset_fixture.h"

#include <spot/assets/asset_file.h>
#include <spot/assets/asset_manifest.h>
#include <spot/assets/asset_pak.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

TEST_CASE("Script assets import Luau files with stable metadata and cached source", "[assets][script]") {
    ScriptAssetFixture fixture;
    const std::string source = "function OnStart() print(\"hello\", nil) end\n";
    CHECK(Spot::GetSourceAssetType("scripts/hello.LUAU") == Spot::AssetType::Script);
    CHECK_FALSE(Spot::GetSourceAssetType("scripts/hello.lua"));
    CHECK_FALSE(Spot::GetSourceAssetType("scripts/hello.vice"));
    CHECK(Spot::ParseAssetType("Script") == Spot::AssetType::Script);
    CHECK(Spot::GetAssetTypeName(Spot::AssetType::Script) == "Script");
    const auto id = fixture.Import("scripts/hello.luau", source);
    REQUIRE(std::filesystem::is_regular_file(fixture.sources / "scripts/hello.luau.meta"));
    REQUIRE(fixture.database.GetMeta(id));
    CHECK(fixture.database.GetMeta(id)->id == id);
    CHECK(fixture.database.GetMeta(id)->type == Spot::AssetType::Script);
    CHECK(fixture.database.Refresh() == 0);

    Spot::Tests::RegisteredAssetManager assets(fixture.imported);
    const auto script = assets.Load<Spot::ScriptSource>("scripts/hello.luau");
    REQUIRE(script);
    CHECK(script.GetId() == id);
    CHECK(script->source == source);
    CHECK(script->sourceName == "scripts/hello.luau");
    CHECK(assets.Load<Spot::ScriptSource>(id).Get() == script.Get());
    CHECK(assets.GetAssetIds(Spot::AssetType::Script, "scripts").size() == 1);

    REQUIRE(fixture.database.Move("scripts/hello.luau", "scripts/renamed.luau"));
    fixture.database.Refresh();
    CHECK(fixture.database.FindByPath("scripts/renamed.luau") == id);
    CHECK_FALSE(fixture.database.FindByPath("scripts/hello.luau"));
    assets.RefreshManifest();
    const auto renamed = assets.Load<Spot::ScriptSource>(id);
    REQUIRE(renamed);
    CHECK(renamed->sourceName == "scripts/renamed.luau");
    CHECK(renamed.Get() != script.Get());
}

TEST_CASE("Script assets support empty files UTF8 BOM and source that is parsed later", "[assets][script]") {
    ScriptAssetFixture fixture;
    fixture.Write("empty.luau", "");
    fixture.Write("bom.luau", "\xEF\xBB\xBF-- UTF-8\r\nfunction start() print(\"\xC3\xA9\") end\r\n");
    fixture.Write("unfinished.luau", "function OnStart()");
    REQUIRE(fixture.database.Refresh() == 3);
    Spot::Tests::RegisteredAssetManager assets(fixture.imported);
    const auto empty = assets.Load<Spot::ScriptSource>("empty.luau");
    REQUIRE(empty);
    CHECK(empty->source.empty());
    const auto bom = assets.Load<Spot::ScriptSource>("bom.luau");
    REQUIRE(bom);
    CHECK(bom->source == "-- UTF-8\r\nfunction start() print(\"\xC3\xA9\") end\r\n");
    const auto unfinished = assets.Load<Spot::ScriptSource>("unfinished.luau");
    REQUIRE(unfinished);
    CHECK(unfinished->source == "function OnStart()");
}

TEST_CASE("Reimporting script assets invalidates the cached source without changing its id", "[assets][script]") {
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("counter.luau", "n = 1");
    Spot::Tests::RegisteredAssetManager assets(fixture.imported);
    const auto old = assets.Load<Spot::ScriptSource>(id);
    REQUIRE(old);
    fixture.Write("counter.luau", "n = 2");
    REQUIRE(fixture.database.Refresh() == 1);
    CHECK(fixture.database.FindByPath("counter.luau") == id);
    assets.RefreshManifest();
    assets.Reload(fixture.database.GetChanged());
    const auto updated = assets.Load<Spot::ScriptSource>(id);
    REQUIRE(updated);
    CHECK(updated.Get() != old.Get());
    CHECK(updated->source == "n = 2");
    CHECK(old->source == "n = 1");
}

TEST_CASE("Script assets load from a shipped pak without source or loose imported files", "[assets][script]") {
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("scripts/shipped.luau", "function OnStart() print(\"shipped\") end");
    const auto shipped = fixture.temp.GetPath() / "shipped";
    std::filesystem::create_directories(shipped);
    std::filesystem::copy_file(Spot::Detail::Manifest::GetPath(fixture.imported),
                               Spot::Detail::Manifest::GetPath(shipped));
    REQUIRE(Spot::Detail::WriteAssetPak(shipped / Spot::Detail::AssetPakFileName, fixture.imported));
    std::filesystem::rename(fixture.sources, fixture.temp.GetPath() / "unavailable_sources");
    Spot::Tests::RegisteredAssetManager assets(shipped);
    const auto script = assets.Load<Spot::ScriptSource>(id);
    REQUIRE(script);
    CHECK(script->source == "function OnStart() print(\"shipped\") end");
    CHECK(script->sourceName == "scripts/shipped.luau");
    CHECK_FALSE(std::filesystem::exists(Spot::Detail::GetImportedAssetPath(shipped, id)));
}

TEST_CASE("Script asset loading rejects corrupt payloads", "[assets][script]") {
    auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks({sink});
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("bad.luau", "");
    for (const bool truncated : {true, false}) {
        Spot::Detail::BinaryWriter writer;
        if (truncated) {
            writer.Write(std::uint32_t{50});
        } else {
            writer.WriteString("function OnStart() end");
            writer.Write(std::uint8_t{0});
        }
        REQUIRE(Spot::Detail::WriteAssetFile(Spot::Detail::GetImportedAssetPath(fixture.imported, id),
                                             Spot::AssetType::Script, writer.GetData()));
        Spot::Tests::RegisteredAssetManager assets(fixture.imported);
        const auto script = assets.Load<Spot::ScriptSource>(id);
        CHECK_FALSE(script);
        CHECK(script.GetId() == id);
    }
}

TEST_CASE("Script assets participate in asynchronous loading and preloading", "[assets][script]") {
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("async.luau", "n = nil");
    Spot::Tests::RegisteredAssetManager assets(fixture.imported);
    const auto request = assets.LoadAsync<Spot::ScriptSource>(id);
    REQUIRE(request.IsValid());
    CHECK_FALSE(assets.IsLoaded(id));
    for (int i = 0; i < 1000 && !request.IsDone(); ++i) {
        assets.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(request.IsDone());
    REQUIRE(request.Get());
    CHECK(request.Get()->source == "n = nil");
    Spot::Tests::RegisteredAssetManager preloadAssets(fixture.imported);
    const std::array ids{id};
    const auto preloaded = preloadAssets.Preload(ids);
    REQUIRE(preloaded.size() == 1);
    REQUIRE(preloadAssets.Load<Spot::ScriptSource>(id));
    CHECK(preloadAssets.Load<Spot::ScriptSource>(id).Get() == preloaded.front().get());
}
