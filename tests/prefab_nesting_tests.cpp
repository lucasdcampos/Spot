#include "log_capture.h"
#include "registered_assets.h"
#include "temp_directory.h"

#include <spot.h>
#include <spot_import.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>

namespace {

using nlohmann::json;

class NestedData final : public Spot::Component {
public:
    float value = 2.0f;
    Spot::EntityId target = Spot::EntityId::Invalid;
    SPOT_FIELDS(value, target)
};

struct NestedProject {
    TempDirectory temp;
    Spot::Scene source;
    Spot::EntityId rootId;
    Spot::AssetId baseId;
    Spot::AssetDatabase database{temp.GetPath() / "assets", temp.GetPath() / "imported"};
    std::unique_ptr<Spot::Tests::RegisteredAssetManager> assets;

    NestedProject() {
        Spot::RegisterComponent<NestedData>("NestedData");
        auto& root = source.CreateEntity("Lamp");
        rootId = root.GetId();
        auto& bulb = root.CreateChild("Bulb");
        root.AddComponent<NestedData>().target = bulb.GetId();
        bulb.AddComponent<NestedData>().target = rootId;
        std::filesystem::create_directories(temp.GetPath() / "assets");
        std::ofstream(File("Lamp.prefab")) << source.SaveToString();
        REQUIRE(database.Refresh() == 1);
        baseId = *database.FindByPath("Lamp.prefab");
        assets = std::make_unique<Spot::Tests::RegisteredAssetManager>(temp.GetPath() / "imported");
    }

    std::filesystem::path File(std::string_view name) const { return temp.GetPath() / "assets" / std::string(name); }
    std::string Read(std::string_view name) const {
        std::ifstream input(File(name), std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
    Spot::AssetId Write(std::string_view name, std::string_view text) {
        std::ofstream(File(name)) << text;
        // Importing fails for prefabs that cannot be flattened, which some tests write.
        database.Refresh();
        assets->RefreshManifest();
        assets->Reload(database.GetChanged());
        const auto id = database.FindByPath(name);
        REQUIRE(id);
        return *id;
    }
    Spot::Entity& Instantiate(Spot::Scene& scene, Spot::AssetId id, Spot::Entity* parent = nullptr) {
        auto* root = scene.Instantiate(assets->Load<Spot::Prefab>(id), {.parent = parent, .assets = assets.get()});
        REQUIRE(root);
        return *root;
    }
    Spot::AssetId Container(Spot::AssetId asset, const std::string& name) {
        Spot::Scene scene;
        auto& root = scene.CreateEntity(name);
        Instantiate(scene, asset, &root);
        return Write(name + ".prefab", Spot::SaveSceneSource(scene, assets.get()));
    }
};

} // namespace

TEST_CASE("Nested prefabs propagate source edits through multiple levels while instance overrides remain",
          "[scene][prefab][nesting]") {
    NestedProject project;
    const auto assembly = project.Container(project.baseId, "Assembly");
    const auto outer = project.Container(assembly, "Outer");
    Spot::Scene scene;
    auto& root = project.Instantiate(scene, outer);
    auto* lamp = root.FindChild("Assembly")->FindChild("Lamp");
    REQUIRE(lamp);
    lamp->GetComponent<NestedData>()->value = 9.0f;
    const auto saved = Spot::SaveSceneSource(scene, project.assets.get());
    CHECK(json::parse(saved)["entities"].size() == 1);
    CHECK(lamp->GetPrefabRootId() == root.GetId());
    CHECK_FALSE(lamp->GetPrefabAssetId().IsValid());
    const auto local = lamp->GetPrefabLocalId();
    project.source.GetEntity(project.rootId)->GetComponent<NestedData>()->value = 5.0f;
    project.source.GetEntity(project.rootId)->CreateChild("New");
    project.Write("Lamp.prefab", project.source.SaveToString());
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved, project.assets.get()));
    auto* changed = loaded.GetRootEntities()[0]->FindChild("Assembly")->FindChild("Lamp");
    REQUIRE(changed);
    CHECK(changed->GetPrefabLocalId() == local);
    CHECK(changed->GetComponent<NestedData>()->value == 9.0f);
    REQUIRE(changed->FindChild("New"));
    CHECK(changed->GetComponent<NestedData>()->target == changed->FindChild("Bulb")->GetId());
    CHECK(changed->FindChild("Bulb")->GetComponent<NestedData>()->target == changed->GetId());
    Spot::Scene fresh;
    auto& freshRoot = project.Instantiate(fresh, outer);
    CHECK(freshRoot.FindChild("Assembly")->FindChild("Lamp")->GetComponent<NestedData>()->value == 5.0f);
}

TEST_CASE("Variants inherit current base data and keep their own overrides independently of scene instances",
          "[scene][prefab][variants]") {
    NestedProject project;
    Spot::Scene authoring;
    auto& root = project.Instantiate(authoring, project.baseId);
    root.SetName("Bright Lamp");
    root.GetComponent<NestedData>()->value = 8.0f;
    const auto variant = project.Write("Bright.prefab", Spot::SaveEntitySource(root, project.assets.get()));
    const auto source = Spot::Prefab::FromString(project.Read("Bright.prefab"));
    REQUIRE(source);
    CHECK(std::ranges::find(source->GetDependencies(), project.baseId) != source->GetDependencies().end());
    // Imported prefabs have the prefabs nested in them expanded.
    const auto loadedAsset = project.assets->Load<Spot::Prefab>(variant);
    REQUIRE(loadedAsset);
    CHECK(loadedAsset->IsFlat());
    CHECK(loadedAsset->GetDependencies().empty());
    Spot::Scene scene;
    auto& instance = project.Instantiate(scene, variant);
    instance.GetComponent<NestedData>()->value = 11.0f;
    const auto saved = Spot::SaveSceneSource(scene, project.assets.get());
    CHECK(instance.GetPrefabAssetId() == variant);
    project.source.GetEntity(project.rootId)->SetName("Changed Base");
    project.source.GetEntity(project.rootId)->GetComponent<NestedData>()->value = 3.0f;
    project.source.GetEntity(project.rootId)->FindChild("Bulb")->SetName("Changed Bulb");
    project.Write("Lamp.prefab", project.source.SaveToString());
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved, project.assets.get()));
    auto& changed = *loaded.GetRootEntities()[0];
    CHECK(changed.GetName() == "Bright Lamp");
    CHECK(changed.GetComponent<NestedData>()->value == 11.0f);
    REQUIRE(changed.FindChild("Changed Bulb"));
    CHECK(changed.GetComponent<NestedData>()->target == changed.FindChild("Changed Bulb")->GetId());
    Spot::Scene fresh;
    CHECK(project.Instantiate(fresh, variant).GetComponent<NestedData>()->value == 8.0f);
    const auto reverted = Spot::PreparePrefabEdit(changed);
    REQUIRE(reverted);
    Spot::Scene clean;
    REQUIRE(clean.LoadFromString(reverted->sceneText, project.assets.get()));
    CHECK(clean.GetRootEntities()[0]->GetComponent<NestedData>()->value == 8.0f);
}

TEST_CASE("New nested source identities stay stable when further children are inserted", "[scene][prefab][nesting]") {
    NestedProject project;
    const auto container = project.Container(project.baseId, "Assembly");
    auto& sourceRoot = *project.source.GetEntity(project.rootId);
    sourceRoot.CreateChild("First New").AddComponent<NestedData>();
    project.Write("Lamp.prefab", project.source.SaveToString());
    Spot::Scene first;
    auto& root = project.Instantiate(first, container);
    auto* child = root.FindChild("Lamp")->FindChild("First New");
    REQUIRE(child);
    const auto local = child->GetPrefabLocalId();
    child->GetComponent<NestedData>()->value = 7.0f;
    const auto saved = Spot::SaveSceneSource(first, project.assets.get());
    sourceRoot.CreateChild("Second New").SetSiblingIndex(0);
    project.Write("Lamp.prefab", project.source.SaveToString());
    Spot::Scene second;
    REQUIRE(second.LoadFromString(saved, project.assets.get()));
    auto* inherited = second.GetRootEntities()[0]->FindChild("Lamp")->FindChild("First New");
    REQUIRE(inherited);
    CHECK(inherited->GetPrefabLocalId() == local);
    CHECK(inherited->GetComponent<NestedData>()->value == 7.0f);
    REQUIRE(second.GetRootEntities()[0]->FindChild("Lamp")->FindChild("Second New"));
}

TEST_CASE("Applying changes to nested prefabs and variants preserves their dependency links",
          "[scene][prefab][nesting][variants]") {
    NestedProject project;
    const auto container = project.Container(project.baseId, "Assembly");
    Spot::Scene scene;
    auto& root = project.Instantiate(scene, container);
    root.FindChild("Lamp")->GetComponent<NestedData>()->value = 12.0f;
    const auto edit = Spot::PreparePrefabEdit(root, {}, true, project.Read("Assembly.prefab"), project.assets.get());
    REQUIRE(edit);
    const auto prefab = Spot::Prefab::FromString(edit->prefabText);
    REQUIRE(prefab);
    CHECK(prefab->GetDependencies().size() == 1);
    CHECK(prefab->GetDependencies()[0] == project.baseId);
    project.Write("Assembly.prefab", edit->prefabText);
    Spot::Scene variantScene;
    auto& variantRoot = project.Instantiate(variantScene, project.baseId);
    variantRoot.SetName("Variant");
    const auto variant = project.Write("Variant.prefab", Spot::SaveEntitySource(variantRoot, project.assets.get()));
    Spot::Scene variantInstance;
    auto& instance = project.Instantiate(variantInstance, variant);
    instance.GetComponent<NestedData>()->value = 14.0f;
    const auto variantEdit =
        Spot::PreparePrefabEdit(instance, {}, true, project.Read("Variant.prefab"), project.assets.get());
    REQUIRE(variantEdit);
    CHECK(json::parse(variantEdit->prefabText)["entities"][0]["prefab"] == project.baseId.ToString());
    project.Write("Variant.prefab", variantEdit->prefabText);
    project.source.GetEntity(project.rootId)->CreateChild("Later");
    project.Write("Lamp.prefab", project.source.SaveToString());
    Spot::Scene fresh;
    auto& assembled = project.Instantiate(fresh, container);
    REQUIRE(assembled.FindChild("Lamp")->FindChild("Later"));
    CHECK(assembled.FindChild("Lamp")->GetComponent<NestedData>()->value == 12.0f);
    auto& changedVariant = project.Instantiate(fresh, variant);
    REQUIRE(changedVariant.FindChild("Later"));
    CHECK(changedVariant.GetComponent<NestedData>()->value == 14.0f);
}

TEST_CASE("Prefab dependency cycles and unavailable nested assets fail to import before changing a scene",
          "[scene][prefab][nesting]") {
    NestedProject project;
    const auto container = project.Container(project.baseId, "Assembly");
    Spot::Scene baseScene;
    auto& instance = project.Instantiate(baseScene, container);
    {
        const auto sink = std::make_shared<Spot::Tests::CaptureSink>();
        const Spot::Tests::ScopedSinks capture({sink});
        project.Write("Lamp.prefab", Spot::SaveEntitySource(instance, project.assets.get()));
        CHECK(std::ranges::any_of(sink->GetRecords(), [](const auto& record) {
            return record.message.find("nests itself") != std::string::npos;
        }));
    }
    Spot::Scene target;
    target.CreateEntity("Sentinel");
    Spot::Scene replacement;
    replacement.CreateEntity("Replacement");
    project.Write("Lamp.prefab", replacement.SaveToString());
    REQUIRE(target.Instantiate(project.assets->Load<Spot::Prefab>(container), {.assets = project.assets.get()}));
    json unavailable = json::parse(project.Read("Assembly.prefab"));
    for (json& node : unavailable["entities"]) {
        if (node.contains("prefab")) {
            node["prefab"] = Spot::AssetId::Generate().ToString();
        }
    }
    const auto missing = project.Write("Missing.prefab", unavailable.dump());
    const auto before = target.SaveToString();
    CHECK_FALSE(target.Instantiate(project.assets->Load<Spot::Prefab>(missing), {.assets = project.assets.get()}));
    CHECK(target.SaveToString() == before);
}

TEST_CASE("Changing a base prefab imports the prefabs nesting it again", "[assets][prefab][nesting]") {
    NestedProject project;
    const auto container = project.Container(project.baseId, "Assembly");
    const auto outer = project.Container(container, "Outer");
    const auto before = project.assets->Load<Spot::Prefab>(outer);
    project.source.GetEntity(project.rootId)->CreateChild("Added");
    project.Write("Lamp.prefab", project.source.SaveToString());
    const auto changed = project.database.GetChanged();
    CHECK(std::ranges::find(changed, container) != changed.end());
    CHECK(std::ranges::find(changed, outer) != changed.end());
    const auto after = project.assets->Load<Spot::Prefab>(outer);
    REQUIRE(after);
    CHECK(before.Get() != after.Get());
    Spot::Scene scene;
    CHECK(project.Instantiate(scene, outer).FindChild("Assembly")->FindChild("Lamp")->FindChild("Added"));
}

TEST_CASE("Nested source metadata and disconnected expanded hierarchies are rejected safely",
          "[scene][prefab][nesting]") {
    NestedProject project;
    Spot::Scene scene;
    auto& root = project.Instantiate(scene, project.baseId);
    json variant = json::parse(Spot::SaveEntitySource(root, project.assets.get()));
    const auto key = variant["entities"][0]["entityIds"].begin().key();
    json duplicate = variant;
    duplicate["entities"][0]["entityIds"]["0" + key] = 1000000;
    CHECK_FALSE(Spot::Prefab::FromString(duplicate.dump()));
    const auto childLocal = root.FindChild("Bulb")->GetPrefabLocalId();
    variant["entities"][0]["overrides"][std::to_string(childLocal)] =
        json::array({{{"op", "replace"}, {"path", "/parent"}, {"value", {{"sceneEntity", 0}}}}});
    const auto invalid = project.Write("Disconnected.prefab", variant.dump());
    const auto before = scene.SaveToString();
    CHECK_FALSE(scene.Instantiate(project.assets->Load<Spot::Prefab>(invalid), {.assets = project.assets.get()}));
    CHECK(scene.SaveToString() == before);
}

TEST_CASE("Excessive prefab nesting fails safely and leaves subsequent instantiation usable",
          "[scene][prefab][nesting]") {
    const auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks capture({sink});
    NestedProject project;
    auto dependency = project.baseId;
    for (int level = 0; level < 64; ++level) {
        const json document{{"format", "spot-scene"},
                            {"version", 2},
                            {"entities", json::array({{{"id", 100},
                                                       {"prefab", dependency.ToString()},
                                                       {"entityIds", json::object()},
                                                       {"overrides", json::object()},
                                                       {"removedEntities", json::array()}}})}};
        dependency = project.Write("Variant" + std::to_string(level) + ".prefab", document.dump());
    }
    Spot::Scene scene;
    scene.CreateEntity("Sentinel");
    CHECK_FALSE(scene.Instantiate(project.assets->Load<Spot::Prefab>(dependency), {.assets = project.assets.get()}));
    CHECK(scene.GetEntityCount() == 1);
    REQUIRE(scene.Instantiate(project.assets->Load<Spot::Prefab>(project.baseId), {.assets = project.assets.get()}));
    CHECK(scene.GetEntityCount() == 3);
}
