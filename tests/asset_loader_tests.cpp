#include "log_capture.h"
#include "temp_directory.h"

#include <spot/assets/asset_archive.h>
#include <spot/assets/asset_file.h>
#include <spot/assets/asset_formats.h>
#include <spot/assets/asset_manifest.h>
#include <spot/assets/assets.h>
#include <spot/core/io/archive_state.h>
#include <spot/runtime/dev_tools_module.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace Spot::Tests {
struct LoaderValue {
    int value = 0;
};
} // namespace Spot::Tests
namespace Spot {
template <>
struct AssetTypeOf<Tests::LoaderValue> {
    static constexpr AssetType Value = AssetType::Script;
};
} // namespace Spot
namespace {
struct LoaderFixture {
    TempDirectory directory;
    Spot::AssetId id = Spot::AssetId::Generate();
    LoaderFixture() {
        Spot::Detail::Manifest manifest;
        Spot::Detail::ManifestEntry entry;
        entry.id = id;
        entry.type = Spot::AssetType::Script;
        entry.path = "value.luau";
        manifest.entries.push_back(std::move(entry));
        REQUIRE(manifest.SaveToFile(Spot::Detail::Manifest::GetPath(directory.GetPath())));
        Spot::Detail::BinaryWriter writer;
        writer.Write(42);
        REQUIRE(Spot::Detail::WriteAssetFile(Spot::Detail::GetImportedAssetPath(directory.GetPath(), id),
                                             Spot::AssetType::Script, writer.GetData()));
    }
};
Spot::AssetLoader ValueLoader(int offset) {
    return Spot::MakeAssetLoader<int>(
        [offset](Spot::AssetId, std::span<const std::byte> data) -> std::optional<int> {
            Spot::Detail::BinaryReader reader(data);
            const int value = reader.Read<int>();
            return reader.IsValid() && reader.IsAtEnd() ? std::optional(value + offset) : std::nullopt;
        },
        [](Spot::AssetManager&, Spot::AssetId, int& value) {
            return std::make_shared<const Spot::Tests::LoaderValue>(Spot::Tests::LoaderValue{value});
        });
}
void Finish(Spot::AssetManager& manager, const Spot::AssetRequest<Spot::Tests::LoaderValue>& request) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!request.IsDone() && std::chrono::steady_clock::now() < deadline) {
        manager.Update();
        std::this_thread::yield();
    }
    REQUIRE(request.IsDone());
}
} // namespace

TEST_CASE("Asset loader registrations are isolated between managers", "[assets][loaders]") {
    LoaderFixture fixture;
    Spot::AssetManager first(fixture.directory.GetPath()), second(fixture.directory.GetPath()),
        bare(fixture.directory.GetPath());
    first.RegisterLoader(Spot::AssetType::Script, ValueLoader(100));
    second.RegisterLoader(Spot::AssetType::Script, ValueLoader(200));
    const auto a = first.Load<Spot::Tests::LoaderValue>(fixture.id);
    const auto b = second.Load<Spot::Tests::LoaderValue>(fixture.id);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->value == 142);
    CHECK(b->value == 242);
    CHECK_FALSE(bare.Load<Spot::Tests::LoaderValue>(fixture.id));
}

TEST_CASE("Asset decoding runs on workers and creation runs on the updating thread", "[assets][loaders]") {
    LoaderFixture fixture;
    Spot::AssetManager manager(fixture.directory.GetPath());
    const auto mainThread = std::this_thread::get_id();
    std::thread::id decodeThread, createThread;
    auto loader = ValueLoader(0);
    const auto prepare = loader.prepare;
    loader.prepare = [&](Spot::AssetId id, std::span<const std::byte> data) {
        decodeThread = std::this_thread::get_id();
        return prepare(id, data);
    };
    const auto create = loader.create;
    loader.create = [&](Spot::AssetManager& assets, Spot::AssetId id, void* data) {
        createThread = std::this_thread::get_id();
        return create(assets, id, data);
    };
    manager.RegisterLoader(Spot::AssetType::Script, std::move(loader));
    const auto request = manager.LoadAsync<Spot::Tests::LoaderValue>(fixture.id);
    Finish(manager, request);
    REQUIRE(request.Get());
    CHECK(request.Get()->value == 42);
    CHECK(decodeThread != mainThread);
    CHECK(createThread == mainThread);
}

TEST_CASE("Failed custom decoders finish async requests without stranding the manager", "[assets][loaders]") {
    LoaderFixture fixture;
    Spot::AssetManager manager(fixture.directory.GetPath());
    auto loader = ValueLoader(0);
    loader.prepare = [](Spot::AssetId, std::span<const std::byte>) -> std::shared_ptr<void> {
        throw std::runtime_error("invalid custom asset");
    };
    manager.RegisterLoader(Spot::AssetType::Script, std::move(loader));
    const auto request = manager.LoadAsync<Spot::Tests::LoaderValue>(fixture.id);
    Finish(manager, request);
    CHECK_FALSE(request.Get());
    CHECK(request.Get().GetId() == fixture.id);
}

TEST_CASE("Asset fields use only their archive manager even when a global manager exists",
          "[assets][loaders][serialization]") {
    LoaderFixture fixture;
    Spot::AssetManager global(fixture.directory.GetPath()), supplied(fixture.directory.GetPath());
    global.RegisterLoader(Spot::AssetType::Script, ValueLoader(100));
    supplied.RegisterLoader(Spot::AssetType::Script, ValueLoader(200));
    struct RestoreManager {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~RestoreManager() { Spot::Detail::SetAssetManager(previous); }
    } restore;
    Spot::Detail::SetAssetManager(&global);
    nlohmann::json document{{"value", fixture.id.ToString()}};
    Spot::AssetRef<Spot::Tests::LoaderValue> unresolved;
    Spot::Detail::ArchiveState noContext{.loading = true, .nodes = {&document}};
    Spot::Archive withoutManager(noContext);
    withoutManager.Field("value", unresolved);
    CHECK_FALSE(unresolved);
    CHECK(unresolved.GetId() == fixture.id);
    CHECK_FALSE(global.IsLoaded(fixture.id));

    Spot::AssetRef<Spot::Tests::LoaderValue> resolved;
    Spot::Detail::ArchiveState context{
        .loading = true, .nodes = {&document}, .context = &supplied, .contextType = &typeid(Spot::AssetManager)};
    Spot::Archive withManager(context);
    withManager.Field("value", resolved);
    REQUIRE(resolved);
    CHECK(resolved->value == 242);
    CHECK_FALSE(global.IsLoaded(fixture.id));
}

TEST_CASE("Registered preload references create dependencies first and propagate reloads", "[assets][loaders]") {
    LoaderFixture fixture;
    const auto child = Spot::AssetId::Generate();
    auto manifest = Spot::Detail::Manifest::LoadFromFile(Spot::Detail::Manifest::GetPath(fixture.directory.GetPath()));
    REQUIRE(manifest);
    Spot::Detail::ManifestEntry entry;
    entry.id = child;
    entry.type = Spot::AssetType::Script;
    entry.path = "child.luau";
    manifest->entries.push_back(std::move(entry));
    REQUIRE(manifest->SaveToFile(Spot::Detail::Manifest::GetPath(fixture.directory.GetPath())));
    Spot::Detail::BinaryWriter writer;
    writer.Write(7);
    REQUIRE(Spot::Detail::WriteAssetFile(Spot::Detail::GetImportedAssetPath(fixture.directory.GetPath(), child),
                                         Spot::AssetType::Script, writer.GetData()));
    Spot::AssetManager manager(fixture.directory.GetPath());
    auto loader = ValueLoader(0);
    loader.references = [child](const void* data) {
        return *static_cast<const int*>(data) == 42 ? std::vector<Spot::AssetId>{child} : std::vector<Spot::AssetId>();
    };
    loader.dependencies = [child](const void* data) {
        return static_cast<const Spot::Tests::LoaderValue*>(data)->value == 42 ? std::vector<Spot::AssetId>{child}
                                                                               : std::vector<Spot::AssetId>();
    };
    std::vector<int> order;
    bool childWasReady = false;
    loader.create = [&](Spot::AssetManager& assets, Spot::AssetId, void* data) -> std::shared_ptr<const void> {
        const int value = *static_cast<const int*>(data);
        if (value == 42)
            childWasReady = assets.IsLoaded(child);
        order.push_back(value);
        return std::make_shared<const Spot::Tests::LoaderValue>(Spot::Tests::LoaderValue{value});
    };
    manager.RegisterLoader(Spot::AssetType::Script, std::move(loader));
    const std::array ids{fixture.id};
    const auto task = manager.StartPreload(ids);
    const auto kept = manager.FinishPreload(*task);
    CHECK(kept.size() == 2);
    CHECK(order == std::vector<int>{7, 42});
    CHECK(childWasReady);
    const auto parent = manager.Load<Spot::Tests::LoaderValue>(fixture.id);
    const auto reloaded = Spot::DevToolsModule::ReloadAssets(manager, std::array{child});
    CHECK(std::ranges::find(reloaded, child) != reloaded.end());
    CHECK(std::ranges::find(reloaded, fixture.id) != reloaded.end());
    CHECK_FALSE(manager.IsLoaded(fixture.id));
    REQUIRE(parent);
    CHECK(parent->value == 42);
}

TEST_CASE("Headless render asset loads skip GPU preparation and retain CPU mesh access", "[assets]") {
    TempDirectory directory;
    const auto id = Spot::AssetId::Generate();
    Spot::Detail::Manifest manifest;
    manifest.entries.push_back({.id = id, .type = Spot::AssetType::Font, .path = "font.font"});
    REQUIRE(manifest.SaveToFile(Spot::Detail::Manifest::GetPath(directory.GetPath())));
    Spot::Detail::BinaryWriter writer;
    Spot::Detail::Encode(writer, Spot::Detail::FontFile{.pixelSize = 32.0f, .fontData = {}});
    REQUIRE(Spot::Detail::WriteAssetFile(Spot::Detail::GetImportedAssetPath(directory.GetPath(), id),
                                         Spot::AssetType::Font, writer.GetData()));
    const auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks{{sink}};
    Spot::AssetManager manager(directory.GetPath());
    Spot::RegisterRenderAssets(manager);
    const auto font = manager.Load<Spot::Font>(id);
    CHECK_FALSE(font);
    CHECK(font.GetId() == id);
    CHECK(std::ranges::any_of(sink->GetRecords(), [](const auto& record) {
        return record.message.find("Font assets need a graphics device") != std::string::npos;
    }));
    const auto mesh = manager.Load<Spot::Mesh>(Spot::BuiltInAssets::CubeMesh);
    CHECK_FALSE(mesh);
    CHECK(mesh.GetId() == Spot::BuiltInAssets::CubeMesh);
    const auto triangles = manager.LoadMeshData(Spot::BuiltInAssets::CubeMesh);
    REQUIRE(triangles);
    CHECK_FALSE(triangles->vertices.empty());
}
