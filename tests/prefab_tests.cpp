#include "log_capture.h"
#include "registered_assets.h"
#include "temp_directory.h"
#include <spot/assets/asset_formats.h>

#include <spot.h>
#include <spot_import.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace {

class PrefabLinks final : public Spot::Component {
public:
    Spot::EntityId internal = Spot::EntityId::Invalid;
    Spot::EntityId external = Spot::EntityId::Invalid;
    Spot::AssetRef<Spot::Material> material;
    int starts = 0;

    SPOT_FIELDS(internal, external, material)

protected:
    void OnStart() override { ++starts; }
};

class PrefabValue final : public Spot::Component {
public:
    int value = 0;

    SPOT_FIELDS(value)
};

std::string MakeTree(Spot::AssetId material = {}) {
    Spot::RegisterComponent<PrefabLinks>("PrefabLinks");
    Spot::Scene source;
    Spot::Entity& parent = source.CreateEntity("Outside");
    Spot::Entity& root = source.CreateEntity("Crate");
    root.SetParent(&parent);
    root.AddTag("Prop");
    root.SetLayer(3);
    root.GetTransform().SetPosition({2.0f, 3.0f, 4.0f});
    root.GetTransform().SetScale({2.0f, 1.0f, 3.0f});
    Spot::Entity& child = root.CreateChild("Handle");
    child.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
    auto& links = root.AddComponent<PrefabLinks>();
    links.internal = child.GetId();
    links.external = parent.GetId();
    links.material = Spot::AssetRef<Spot::Material>(material, nullptr);
    auto& disabled = child.AddComponent<PrefabLinks>();
    disabled.internal = root.GetId();
    disabled.SetEnabled(false);
    root.CreateChild("Hidden").SetActive(false);
    return root.SaveToString();
}

Spot::AssetRef<Spot::Prefab> MakePrefab() {
    auto prefab = Spot::Prefab::FromString(MakeTree());
    REQUIRE(prefab);
    return std::make_shared<const Spot::Prefab>(std::move(*prefab));
}

} // namespace

TEST_CASE("Prefab instances keep the tree and remap references to independent ids", "[scene][prefab]") {
    const auto prefab = MakePrefab();
    Spot::Scene scene;
    Spot::Entity& parent = scene.CreateEntity("Destination");
    parent.CreateChild("Before");
    parent.GetTransform().SetPosition({10.0f, 0.0f, 0.0f});
    auto* first = scene.Instantiate(prefab, {.parent = &parent, .siblingIndex = 0});
    auto* second = scene.Instantiate(prefab, {.parent = &parent, .keepIds = true, .keepExternalReferences = true});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->GetId() != second->GetId());
    CHECK(first->GetParent() == &parent);
    CHECK(first->GetSiblingIndex() == 0);
    CHECK(second->GetSiblingIndex() == 2);
    CHECK(first->GetName() == "Crate");
    CHECK(first->HasTag("Prop"));
    CHECK(first->GetLayer() == 3);
    CHECK(first->GetTransform().GetScale() == Spot::Vector3{2.0f, 1.0f, 3.0f});
    REQUIRE(first->GetChildren().size() == 2);
    CHECK(first->GetChildren()[0]->GetName() == "Handle");
    CHECK_FALSE(first->GetChildren()[1]->IsActiveSelf());
    CHECK(first->GetChildren()[0]->GetTransform().GetWorldPosition() == Spot::Vector3{12.0f, 4.0f, 4.0f});
    auto* links = first->GetComponent<PrefabLinks>();
    REQUIRE(links);
    CHECK(links->internal == first->GetChildren()[0]->GetId());
    CHECK(links->external == Spot::EntityId::Invalid);
    CHECK(second->GetComponent<PrefabLinks>()->internal == second->GetChildren()[0]->GetId());
    CHECK(links->starts == 0);
    auto* disabled = first->GetChildren()[0]->GetComponent<PrefabLinks>();
    REQUIRE(disabled);
    CHECK(disabled->internal == first->GetId());
    CHECK_FALSE(disabled->IsEnabled());
    scene.Update(0.0f);
    CHECK(links->starts == 1);
    CHECK(disabled->starts == 0);
    const auto secondId = second->GetId();
    first->Destroy();
    CHECK(scene.GetEntity(secondId) == second);
    CHECK(scene.GetEntityCount() == 5);
}

TEST_CASE("Invalid prefab documents are rejected before entities can be created", "[assets][prefab]") {
    const auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks capture({sink});
    const std::array<std::string_view, 15> invalid{
        "not json",
        "[]",
        "{}",
        R"({"format":"other","version":1,"entities":[{"id":1}]})",
        R"({"format":"spot-scene","version":99,"entities":[{"id":1}]})",
        R"({"format":"spot-scene","version":1.0,"entities":[{"id":1}]})",
        R"({"format":"spot-scene","version":1,"entities":[]})",
        R"({"format":"spot-scene","version":1,"entities":[null]})",
        R"({"format":"spot-scene","version":1,"entities":[{"id":0}]})",
        R"({"format":"spot-scene","version":1,"entities":[{"id":-1}]})",
        R"({"format":"spot-scene","version":1,"entities":[{"id":1},{"id":1,"parent":1}]})",
        R"({"format":"spot-scene","version":1,"entities":[{"id":1},{"id":2}]})",
        R"({"format":"spot-scene","version":1,"entities":[{"id":1,"parent":"bad"}]})",
        R"({"format":"spot-scene","version":2,"entities":[{"id":1,"prefab":"0123456789abcdef0123456789abcdef"}]})",
        R"({"format":"spot-scene","version":1,"entities":[{"id":1},{"id":2,"parent":3},{"id":3,"parent":2}]})"};
    for (const auto text : invalid) {
        CAPTURE(text);
        CHECK_FALSE(Spot::Prefab::FromString(text));
    }
    CHECK_FALSE(Spot::Prefab::FromString(R"({"format":"spot-scene","version":1,"entities":[{"id":1,"parent":1}]})"));
    Spot::Scene scene;
    scene.CreateEntity("Existing");
    CHECK_FALSE(scene.Instantiate(Spot::AssetRef<Spot::Prefab>(Spot::AssetId::Generate(), nullptr)));
    CHECK(scene.GetEntityCount() == 1);
    CHECK_FALSE(sink->GetRecords().empty());
}

TEST_CASE("Prefab assets import, share, reload and instantiate without source files or a GPU", "[assets][prefab]") {
    const TempDirectory temp;
    const auto sources = temp.GetPath() / "assets";
    const auto imported = temp.GetPath() / "imported";
    std::filesystem::create_directories(sources);
    REQUIRE(Spot::MaterialSource{.baseColor = Spot::Color::Red}.SaveToFile(sources / "Red.material"));
    Spot::AssetDatabase database(sources, imported);
    REQUIRE(database.Refresh() == 1);
    const auto material = database.FindByPath("Red.material");
    REQUIRE(material);
    std::ofstream(sources / "Crate.prefab") << MakeTree(*material);
    REQUIRE(database.Refresh() == 1);
    const auto id = database.FindByPath("Crate.prefab");
    REQUIRE(id);
    REQUIRE(database.GetMeta(*id));
    CHECK(database.GetMeta(*id)->type == Spot::AssetType::Prefab);
    CHECK(database.Find(*id)->type == Spot::AssetType::Prefab);
    CHECK(database.Refresh() == 0);
    Spot::Tests::RegisteredAssetManager assets(imported);
    const auto prefab = assets.Load<Spot::Prefab>(*id);
    REQUIRE(prefab);
    CHECK(assets.Load<Spot::Prefab>("Crate.prefab").Get() == prefab.Get());
    CHECK_FALSE(assets.Load<Spot::Prefab>(*material));
    std::filesystem::remove_all(sources);
    Spot::Scene scene;
    const auto* root = scene.Instantiate(prefab, {.assets = &assets});
    REQUIRE(root);
    const auto* links = root->GetComponent<PrefabLinks>();
    REQUIRE(links);
    REQUIRE(links->material);
    CHECK(links->material.GetId() == *material);
    CHECK(links->material->baseColor == Spot::Color::Red);
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString(), &assets));
    CHECK(loaded.FindEntity("Crate")->GetComponent<PrefabLinks>()->internal == loaded.FindEntity("Handle")->GetId());
    const std::array changed{*id};
    Spot::DevToolsModule::ReloadAssets(assets, changed);
    const auto reloaded = assets.Load<Spot::Prefab>(*id);
    REQUIRE(reloaded);
    CHECK(reloaded.Get() != prefab.Get());
    CHECK(reloaded->GetEntityText() == prefab->GetEntityText());
}

TEST_CASE("Prefab importing rejects invalid trees and runtime rejects corrupt imported payloads", "[assets][prefab]") {
    const TempDirectory temp;
    const auto sources = temp.GetPath() / "assets";
    const auto imported = temp.GetPath() / "imported";
    std::filesystem::create_directories(sources);
    const auto file = sources / "Crate.prefab";
    std::ofstream(file) << R"({"format":"spot-scene","version":1,"entities":[{"id":1},{"id":2}]})";
    Spot::AssetDatabase database(sources, imported);
    CHECK(database.Refresh() == 0);
    std::ofstream(file) << MakeTree();
    REQUIRE(database.Refresh() == 1);
    const auto id = database.FindByPath("Crate.prefab");
    REQUIRE(id);
    Spot::Detail::BinaryWriter data;
    SECTION("Truncated binary") {
        data.Write<std::uint32_t>(100);
    }
    SECTION("Invalid tree") {
        Spot::Detail::EncodeScene(data, R"({"format":"spot-scene","version":1,"entities":[]})");
    }
    REQUIRE(Spot::Detail::WriteAssetFile(Spot::Detail::GetImportedAssetPath(imported, *id), Spot::AssetType::Prefab,
                                         data.GetData()));
    Spot::Tests::RegisteredAssetManager assets(imported);
    const auto prefab = assets.Load<Spot::Prefab>(*id);
    CHECK_FALSE(prefab);
    CHECK(prefab.GetId() == *id);
    Spot::Scene scene;
    CHECK_FALSE(scene.Instantiate(prefab));
    CHECK(scene.GetEntityCount() == 0);
}

TEST_CASE("Prefabs keep unavailable component data for tools until game code registers its types", "[assets][prefab]") {
    const auto parsed = Spot::Prefab::FromString(R"({"format":"spot-scene","version":1,"entities":[
        {"id":42,"name":"Future","components":[{"type":"FuturePrefabComponent","enabled":false,"data":{"value":9}}]}]})");
    REQUIRE(parsed);
    Spot::Scene scene;
    // Games skip them.
    auto* skipped = scene.Instantiate(std::make_shared<const Spot::Prefab>(*parsed));
    REQUIRE(skipped);
    CHECK_FALSE(skipped->GetComponent<Spot::MissingComponent>());
    auto* root = scene.Instantiate(std::make_shared<const Spot::Prefab>(*parsed), {.keepUnknownComponents = true});
    REQUIRE(root);
    const auto* missing = root->GetComponent<Spot::MissingComponent>();
    REQUIRE(missing);
    CHECK(missing->GetTypeName() == "FuturePrefabComponent");
    CHECK_FALSE(missing->IsEnabled());
    CHECK(Spot::SaveComponentToString(*missing).find("9") != std::string::npos);
    Spot::RegisterComponent<PrefabValue>("FuturePrefabComponent");
    auto* next = scene.Instantiate(std::make_shared<const Spot::Prefab>(*parsed));
    REQUIRE(next);
    const auto* value = next->GetComponent<PrefabValue>();
    REQUIRE(value);
    CHECK(value->value == 9);
    CHECK_FALSE(value->IsEnabled());
    Spot::UnregisterComponent("FuturePrefabComponent");
}

TEST_CASE("Prefab validation accepts a root after its child and full-width entity ids", "[assets][prefab]") {
    const auto prefab = Spot::Prefab::FromString(R"({"format":"spot-scene","version":1,"entities":[
        {"id":1,"name":"Child","parent":18446744073709551615},
        {"id":18446744073709551615,"name":"Root"}]})");
    REQUIRE(prefab);
    Spot::Scene scene;
    auto* root = scene.Instantiate(std::make_shared<const Spot::Prefab>(*prefab));
    REQUIRE(root);
    CHECK(root->GetName() == "Root");
    REQUIRE(root->GetChildren().size() == 1);
    CHECK(root->GetChildren()[0]->GetName() == "Child");
}

TEST_CASE("Prefab component archives use the resolved running asset manager", "[assets][prefab]") {
    const TempDirectory temp;
    const auto sources = temp.GetPath() / "assets";
    const auto imported = temp.GetPath() / "imported";
    std::filesystem::create_directories(sources);
    REQUIRE(Spot::MaterialSource{.baseColor = Spot::Color::Red}.SaveToFile(sources / "Red.material"));
    Spot::AssetDatabase database(sources, imported);
    REQUIRE(database.Refresh() == 1);
    const auto materialId = database.FindByPath("Red.material");
    REQUIRE(materialId);
    std::ofstream(sources / "Crate.prefab") << MakeTree(*materialId);
    REQUIRE(database.Refresh() == 1);
    const auto crateId = database.FindByPath("Crate.prefab");
    REQUIRE(crateId);
    Spot::Tests::RegisteredAssetManager assets(imported);

    Spot::AssetId prefabId = *crateId;
    SECTION("A component of the instantiated prefab") {
    }
    SECTION("A component inherited from a nested prefab") {
        Spot::Scene authoring;
        auto& container = authoring.CreateEntity("Container");
        REQUIRE(authoring.Instantiate(assets.Load<Spot::Prefab>(*crateId), {.parent = &container, .assets = &assets}));
        std::ofstream(sources / "Container.prefab") << Spot::SaveSceneSource(authoring, &assets);
        REQUIRE(database.Refresh() == 1);
        const auto containerId = database.FindByPath("Container.prefab");
        REQUIRE(containerId);
        assets.RefreshManifest();
        prefabId = *containerId;
    }

    struct RestoreAssets {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~RestoreAssets() { Spot::Detail::SetAssetManager(previous); }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    const auto prefab = assets.Load<Spot::Prefab>(prefabId);
    REQUIRE(prefab);
    Spot::Scene scene;
    REQUIRE(scene.Instantiate(prefab));
    const auto* crate = scene.FindEntity("Crate");
    REQUIRE(crate);
    const auto* links = crate->GetComponent<PrefabLinks>();
    REQUIRE(links);
    CHECK(links->material.GetId() == *materialId);
    REQUIRE(links->material);
    CHECK(links->material->baseColor == Spot::Color::Red);
    CHECK(links->material.Get() == assets.Load<Spot::Material>(*materialId).Get());
}
