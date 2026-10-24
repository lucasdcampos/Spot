#include "log_capture.h"
#include "registered_assets.h"
#include "temp_directory.h"

#include <spot.h>
#include <spot_import.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

using nlohmann::json;

class PrefabData final : public Spot::Component {
public:
    float value = 1.0f;
    std::string label = "Default";
    Spot::EntityId target = Spot::EntityId::Invalid;

    SPOT_FIELDS(value, label, target)
};

struct PrefabProject {
    TempDirectory temp;
    Spot::Scene source;
    Spot::EntityId rootId;
    Spot::AssetId assetId;
    std::unique_ptr<Spot::AssetDatabase> database;
    std::unique_ptr<Spot::Tests::RegisteredAssetManager> assets;

    PrefabProject() {
        Spot::RegisterComponent<PrefabData>("PrefabOverrideData");
        auto& root = source.CreateEntity("Prop");
        rootId = root.GetId();
        auto& part = root.CreateChild("Part");
        part.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
        part.AddComponent<PrefabData>().target = rootId;
        root.CreateChild("Removed").CreateChild("Nested");
        root.AddComponent<PrefabData>().target = part.GetId();
        root.AddComponent<PrefabData>().value = 2.0f;
        std::filesystem::create_directories(temp.GetPath() / "assets");
        REQUIRE(source.SaveToFile(GetFile()));
        database = std::make_unique<Spot::AssetDatabase>(temp.GetPath() / "assets", temp.GetPath() / "imported");
        REQUIRE(database->Refresh() == 1);
        const auto id = database->FindByPath("Prop.prefab");
        REQUIRE(id);
        assetId = *id;
        assets = std::make_unique<Spot::Tests::RegisteredAssetManager>(temp.GetPath() / "imported");
    }

    std::filesystem::path GetFile() const { return temp.GetPath() / "assets" / "Prop.prefab"; }
    Spot::Entity& GetRoot() const { return *source.GetEntity(rootId); }
    Spot::Entity& Instantiate(Spot::Scene& scene) const {
        auto* root = scene.Instantiate(assets->Load<Spot::Prefab>(assetId), {.assets = assets.get()});
        REQUIRE(root);
        return *root;
    }
    void Reimport() {
        REQUIRE(source.SaveToFile(GetFile()));
        REQUIRE(database->Refresh() == 1);
        assets->RefreshManifest();
        const std::array changed{assetId};
        Spot::DevToolsModule::ReloadAssets(*assets, changed);
    }
};

PrefabData* FindData(Spot::Entity& entity, Spot::ComponentId id) {
    for (auto* component : entity.GetComponents<PrefabData>()) {
        if (component->GetId() == id) {
            return component;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Unchanged prefab instances save only identity and no overrides", "[scene][prefab][overrides]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& root = project.Instantiate(scene);
    CHECK(root.GetPrefabAssetId() == project.assetId);
    CHECK(root.GetPrefabRootId() == root.GetId());
    CHECK(root.GetPrefabLocalId() == static_cast<std::uint64_t>(project.rootId));
    const auto* child = root.FindChild("Part");
    REQUIRE(child);
    CHECK(child->GetPrefabRootId() == root.GetId());
    CHECK(child->GetPrefabLocalId() == static_cast<std::uint64_t>(project.GetRoot().FindChild("Part")->GetId()));
    const json saved = json::parse(Spot::SaveSceneSource(scene, project.assets.get()));
    CHECK(saved["version"] == 2);
    REQUIRE(saved["entities"].size() == 1);
    const auto& record = saved["entities"][0];
    CHECK(record["prefab"] == project.assetId.ToString());
    CHECK(record["entityIds"].size() == 4);
    CHECK(record["overrides"].empty());
    CHECK(record["removedEntities"].empty());
    CHECK_FALSE(record.contains("components"));
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved.dump(), project.assets.get()));
    CHECK(loaded.GetEntityCount() == 4);
    CHECK(loaded.GetRootEntities()[0]->GetPrefabAssetId() == project.assetId);
    CHECK(json::parse(Spot::SaveSceneSource(loaded, project.assets.get()))["entities"][0]["overrides"].empty());
    // Saved by the engine, instances keep their links without their prefab's data being compared.
    const json linked = json::parse(scene.SaveToString());
    REQUIRE(linked["entities"].size() == 4);
    CHECK(linked["entities"][0]["prefabAsset"] == project.assetId.ToString());
    CHECK(linked["entities"][1]["prefabRoot"] == static_cast<std::uint64_t>(root.GetId()));
    Spot::Scene copy;
    REQUIRE(copy.LoadFromString(linked.dump(), project.assets.get()));
    CHECK(copy.GetRootEntities()[0]->GetPrefabAssetId() == project.assetId);
    CHECK(copy.GetRootEntities()[0]->FindChild("Part")->GetPrefabRootId() == copy.GetRootEntities()[0]->GetId());
}

TEST_CASE("Prefab editing prepares field and component changes without mutating the scene",
          "[scene][prefab][editing]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& root = project.Instantiate(scene);
    auto* data = root.GetComponents<PrefabData>()[0];
    const auto id = data->GetId();
    data->value = 9.0f;
    data->label = "Custom";
    root.SetName("Instance");
    const auto before = scene.SaveToString();
    const Spot::PrefabOverrideScope field{root.GetPrefabLocalId(), "/components/" + id.ToString() + "/data/value"};
    const auto revert = Spot::PreparePrefabEdit(root, field);
    REQUIRE(revert);
    CHECK(revert->prefabText.empty());
    CHECK(scene.SaveToString() == before);
    Spot::Scene reverted;
    REQUIRE(reverted.LoadFromString(revert->sceneText, project.assets.get()));
    auto& revertedRoot = *reverted.GetRootEntities()[0];
    REQUIRE(FindData(revertedRoot, id));
    CHECK(FindData(revertedRoot, id)->value == 1.0f);
    CHECK(FindData(revertedRoot, id)->label == "Custom");
    CHECK(revertedRoot.GetName() == "Instance");

    const auto apply = Spot::PreparePrefabEdit(root, field, true);
    REQUIRE(apply);
    CHECK(scene.SaveToString() == before);
    std::ofstream(project.GetFile()) << apply->prefabText;
    REQUIRE(project.database->Refresh() == 1);
    project.assets->RefreshManifest();
    Spot::DevToolsModule::ReloadAssets(*project.assets, std::array{project.assetId});
    Spot::Scene applied;
    REQUIRE(applied.LoadFromString(apply->sceneText, project.assets.get()));
    auto& appliedRoot = *applied.GetRootEntities()[0];
    CHECK(FindData(appliedRoot, id)->value == 9.0f);
    CHECK(FindData(appliedRoot, id)->label == "Custom");
    CHECK(appliedRoot.GetName() == "Instance");
    Spot::Scene fresh;
    auto& freshRoot = project.Instantiate(fresh);
    CHECK(FindData(freshRoot, id)->value == 9.0f);
    CHECK(FindData(freshRoot, id)->label == "Default");
    CHECK(freshRoot.GetName() == "Prop");
    REQUIRE(freshRoot.FindChild("Part"));
    CHECK(FindData(freshRoot, id)->target == freshRoot.FindChild("Part")->GetId());

    const auto component = Spot::PreparePrefabEdit(root, {root.GetPrefabLocalId(), "/components/" + id.ToString()});
    REQUIRE(component);
    Spot::Scene componentReverted;
    REQUIRE(componentReverted.LoadFromString(component->sceneText, project.assets.get()));
    CHECK(FindData(*componentReverted.GetRootEntities()[0], id)->label == "Default");
    CHECK(componentReverted.GetRootEntities()[0]->GetName() == "Instance");
}

TEST_CASE("Applying structural prefab edits preserves identities and converts added entity references",
          "[scene][prefab][editing]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& root = project.Instantiate(scene);
    const auto rootId = root.GetId();
    const auto firstId = root.GetComponents<PrefabData>()[0]->GetId();
    root.RemoveComponent(*root.GetComponents<PrefabData>()[1]);
    scene.DestroyEntity(*root.FindChild("Removed"));
    auto& added = root.CreateChild("Added");
    const auto addedId = added.GetId();
    added.CreateChild("Added Child");
    root.GetComponents<PrefabData>()[0]->target = addedId;
    added.AddComponent<PrefabData>().target = rootId;
    const auto edit = Spot::PreparePrefabEdit(root, {}, true);
    REQUIRE(edit);
    std::ofstream(project.GetFile()) << edit->prefabText;
    REQUIRE(project.database->Refresh() == 1);
    project.assets->RefreshManifest();
    Spot::DevToolsModule::ReloadAssets(*project.assets, std::array{project.assetId});
    Spot::Scene applied;
    REQUIRE_FALSE(applied.Instantiate(edit->sceneText, {.keepIds = true, .assets = project.assets.get()}).empty());
    auto* appliedRoot = applied.GetEntity(rootId);
    REQUIRE(appliedRoot);
    CHECK(applied.GetEntity(addedId) == appliedRoot->FindChild("Added"));
    CHECK(Spot::GetPrefabOverrides(*appliedRoot).empty());
    CHECK(FindData(*appliedRoot, firstId)->target == addedId);
    CHECK(appliedRoot->FindChild("Added")->GetComponent<PrefabData>()->target == rootId);
    CHECK(appliedRoot->GetComponents<PrefabData>().size() == 1);
    CHECK_FALSE(appliedRoot->FindChild("Removed"));
    REQUIRE(appliedRoot->FindChild("Added")->FindChild("Added Child"));
    Spot::Scene fresh;
    auto& freshRoot = project.Instantiate(fresh);
    REQUIRE(freshRoot.FindChild("Added"));
    CHECK(FindData(freshRoot, firstId)->target == freshRoot.FindChild("Added")->GetId());
    CHECK(freshRoot.FindChild("Added")->GetComponent<PrefabData>()->target == freshRoot.GetId());
}

TEST_CASE("Reverting an instance removes its additions and restores inherited entities and components",
          "[scene][prefab][editing]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& root = project.Instantiate(scene);
    scene.DestroyEntity(*root.FindChild("Removed"));
    root.CreateChild("Added").CreateChild("Added Child");
    root.RemoveComponent(*root.GetComponents<PrefabData>()[0]);
    root.AddComponent<PrefabData>().value = 99.0f;
    const auto revert = Spot::PreparePrefabEdit(root);
    REQUIRE(revert);
    Spot::Scene restored;
    REQUIRE(restored.LoadFromString(revert->sceneText, project.assets.get()));
    auto& restoredRoot = *restored.GetRootEntities()[0];
    CHECK(restored.GetEntityCount() == 4);
    CHECK_FALSE(restoredRoot.FindChild("Added"));
    REQUIRE(restoredRoot.FindChild("Removed"));
    REQUIRE(restoredRoot.FindChild("Removed")->FindChild("Nested"));
    CHECK(restoredRoot.GetComponents<PrefabData>().size() == 2);
    CHECK(Spot::GetPrefabOverrides(restoredRoot).empty());
    CHECK_FALSE(Spot::PreparePrefabEdit(restoredRoot));
}

TEST_CASE("Unpacking one prefab leaves other instances linked and preserves ids and references",
          "[scene][prefab][editing]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& first = project.Instantiate(scene);
    auto& second = project.Instantiate(scene);
    const auto firstId = first.GetId();
    const auto secondId = second.GetId();
    first.CreateChild("Added");
    first.SetName("Unpacked");
    const auto unpack = Spot::UnpackPrefabToString(first);
    REQUIRE(unpack);
    Spot::Scene loaded;
    REQUIRE_FALSE(loaded.Instantiate(*unpack, {.keepIds = true, .assets = project.assets.get()}).empty());
    REQUIRE(loaded.GetEntity(firstId));
    CHECK_FALSE(loaded.GetEntity(firstId)->GetPrefabAssetId().IsValid());
    CHECK(loaded.GetEntity(firstId)->FindChild("Part")->GetPrefabRootId() == Spot::EntityId::Invalid);
    CHECK(loaded.GetEntity(firstId)->GetComponent<PrefabData>()->target ==
          loaded.GetEntity(firstId)->FindChild("Part")->GetId());
    CHECK(loaded.GetEntity(firstId)->FindChild("Added"));
    REQUIRE(loaded.GetEntity(secondId));
    CHECK(loaded.GetEntity(secondId)->GetPrefabAssetId() == project.assetId);
    CHECK_FALSE(Spot::UnpackPrefabToString(*loaded.GetEntity(firstId)));
}

TEST_CASE("Applying one removed prefab parent removes its source subtree without applying other fields",
          "[scene][prefab][editing]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& root = project.Instantiate(scene);
    auto* removed = root.FindChild("Removed");
    REQUIRE(removed);
    const auto local = removed->GetPrefabLocalId();
    scene.DestroyEntity(*removed);
    root.SetName("Custom");
    const auto edit = Spot::PreparePrefabEdit(root, {local, {}}, true);
    REQUIRE(edit);
    const auto prefab = Spot::Prefab::FromString(edit->prefabText);
    REQUIRE(prefab);
    Spot::Scene source;
    REQUIRE_FALSE(source.Instantiate(prefab->GetEntityText()).empty());
    CHECK(source.GetEntityCount() == 2);
    CHECK(source.GetRootEntities()[0]->GetName() == "Prop");
    const json saved = json::parse(edit->sceneText);
    CHECK(saved["entities"][0]["overrides"].contains(std::to_string(root.GetPrefabLocalId())));
}

TEST_CASE("Prefab source edits propagate while fields and stable component overrides remain",
          "[scene][prefab][overrides]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& first = project.Instantiate(scene);
    auto& second = project.Instantiate(scene);
    const auto firstId = first.GetComponents<PrefabData>()[0]->GetId();
    const auto secondId = first.GetComponents<PrefabData>()[1]->GetId();
    first.SetName("Custom");
    FindData(first, firstId)->value = 7.0f;
    const auto saved = Spot::SaveSceneSource(scene, project.assets.get());
    auto& source = project.GetRoot();
    source.SetName("Updated");
    FindData(source, firstId)->value = 3.0f;
    FindData(source, firstId)->label = "Changed";
    FindData(source, secondId)->value = 4.0f;
    source.FindChild("Part")->GetTransform().SetPosition({0.0f, 2.0f, 0.0f});
    auto& inserted = source.AddComponent<PrefabData>();
    inserted.value = 11.0f;
    source.MoveComponent(inserted, 0);
    source.CreateChild("New source child");
    project.Reimport();
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved, project.assets.get()));
    auto* custom = loaded.FindEntity("Custom");
    auto* updated = loaded.FindEntity("Updated");
    REQUIRE(custom);
    REQUIRE(updated);
    CHECK(FindData(*custom, firstId)->value == 7.0f);
    CHECK(FindData(*custom, firstId)->label == "Changed");
    CHECK(FindData(*custom, secondId)->value == 4.0f);
    CHECK(FindData(*updated, firstId)->value == 3.0f);
    CHECK(updated->GetComponents<PrefabData>()[0]->GetId() == inserted.GetId());
    CHECK(custom->FindChild("Part")->GetTransform().GetPosition().y == 2.0f);
    CHECK(custom->FindChild("New source child"));
    CHECK(updated->FindChild("New source child"));
    const auto again = Spot::SaveSceneSource(loaded, project.assets.get());
    Spot::Scene roundTrip;
    REQUIRE(roundTrip.LoadFromString(again, project.assets.get()));
    CHECK(FindData(*roundTrip.FindEntity("Custom"), firstId)->value == 7.0f);
    CHECK(first.GetId() != second.GetId());
}

TEST_CASE("Prefab structural overrides keep additions and removals through source changes",
          "[scene][prefab][overrides]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& root = project.Instantiate(scene);
    const auto removedComponent = root.GetComponents<PrefabData>()[0]->GetId();
    const auto keptComponent = root.GetComponents<PrefabData>()[1]->GetId();
    root.RemoveComponent(*FindData(root, removedComponent));
    root.FindChild("Removed")->Destroy();
    auto& extra = root.CreateChild("Extra");
    extra.SetSiblingIndex(0);
    extra.AddComponent<PrefabData>().target = root.FindChild("Part")->GetId();
    auto& added = root.AddComponent<PrefabData>();
    added.value = 8.0f;
    added.target = extra.GetId();
    const auto addedId = added.GetId();
    FindData(root, keptComponent)->target = extra.GetId();
    root.MoveComponent(added, 0);
    const auto saved = Spot::SaveSceneSource(scene, project.assets.get());
    project.GetRoot().FindChild("Removed")->CreateChild("Also removed");
    project.GetRoot().CreateChild("From source");
    auto& sourceComponent = project.GetRoot().AddComponent<PrefabData>();
    sourceComponent.value = 15.0f;
    const auto sourceId = sourceComponent.GetId();
    project.Reimport();
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved, project.assets.get()));
    auto* instance = loaded.FindEntity("Prop");
    REQUIRE(instance);
    CHECK_FALSE(FindData(*instance, removedComponent));
    CHECK_FALSE(instance->FindChild("Removed"));
    CHECK_FALSE(loaded.FindEntity("Also removed"));
    CHECK(instance->FindChild("From source"));
    auto* loadedExtra = instance->FindChild("Extra");
    REQUIRE(loadedExtra);
    CHECK(instance->GetChildren()[0] == loadedExtra);
    CHECK(FindData(*instance, addedId)->value == 8.0f);
    CHECK(FindData(*instance, addedId)->target == loadedExtra->GetId());
    CHECK(FindData(*instance, keptComponent)->target == loadedExtra->GetId());
    CHECK(loadedExtra->GetComponent<PrefabData>()->target == instance->FindChild("Part")->GetId());
    CHECK(FindData(*instance, sourceId)->value == 15.0f);
    CHECK(instance->GetComponents<PrefabData>()[0]->GetId() == addedId);
    Spot::Scene roundTrip;
    REQUIRE(roundTrip.LoadFromString(Spot::SaveSceneSource(loaded, project.assets.get()), project.assets.get()));
    CHECK_FALSE(roundTrip.FindEntity("Removed"));
    CHECK(roundTrip.FindEntity("Extra"));
    CHECK(roundTrip.FindEntity("From source"));
}

TEST_CASE("Overrides of removed prefab targets are dropped and do not affect replacement components",
          "[scene][prefab][overrides]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& root = project.Instantiate(scene);
    const auto componentId = root.GetComponents<PrefabData>()[0]->GetId();
    FindData(root, componentId)->value = 7.0f;
    root.FindChild("Part")->SetName("Custom part");
    const auto saved = Spot::SaveSceneSource(scene, project.assets.get());
    project.GetRoot().FindChild("Part")->Destroy();
    project.GetRoot().RemoveComponent(*FindData(project.GetRoot(), componentId));
    auto& replacement = project.GetRoot().AddComponent<PrefabData>();
    replacement.value = 20.0f;
    const auto replacementId = replacement.GetId();
    project.Reimport();
    const auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks capture({sink});
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved, project.assets.get()));
    auto* instance = loaded.FindEntity("Prop");
    REQUIRE(instance);
    CHECK_FALSE(instance->FindChild("Custom part"));
    CHECK_FALSE(FindData(*instance, componentId));
    CHECK(FindData(*instance, replacementId)->value == 20.0f);
    CHECK_FALSE(sink->GetRecords().empty());
    const json resaved = json::parse(Spot::SaveSceneSource(loaded, project.assets.get()));
    CHECK(resaved["entities"][0]["overrides"].empty());
}

TEST_CASE("Prefab local references and references between instances remap without collisions",
          "[scene][prefab][overrides]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& first = project.Instantiate(scene);
    auto& second = project.Instantiate(scene);
    first.SetName("First");
    second.SetName("Second");
    const auto componentId = first.GetComponents<PrefabData>()[0]->GetId();
    FindData(first, componentId)->target = second.FindChild("Part")->GetId();
    second.FindChild("Part")->GetComponent<PrefabData>()->target = first.FindChild("Part")->GetId();
    auto& outside = scene.CreateEntity("Outside");
    outside.AddComponent<PrefabData>().target = first.FindChild("Part")->GetId();
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(Spot::SaveSceneSource(scene, project.assets.get()), project.assets.get()));
    auto* loadedFirst = loaded.FindEntity("First");
    auto* loadedSecond = loaded.FindEntity("Second");
    REQUIRE(loadedFirst);
    REQUIRE(loadedSecond);
    CHECK(FindData(*loadedFirst, componentId)->target == loadedSecond->FindChild("Part")->GetId());
    CHECK(loadedSecond->FindChild("Part")->GetComponent<PrefabData>()->target ==
          loadedFirst->FindChild("Part")->GetId());
    CHECK(loadedFirst->FindChild("Part")->GetComponent<PrefabData>()->target == loadedFirst->GetId());
    CHECK(loaded.FindEntity("Outside")->GetComponent<PrefabData>()->target == loadedFirst->FindChild("Part")->GetId());
    CHECK(loadedFirst->GetId() != first.GetId());
}

TEST_CASE("Prefab children copied independently and expanded snapshots become ordinary trees",
          "[scene][prefab][overrides]") {
    PrefabProject project;
    Spot::Scene scene;
    auto& root = project.Instantiate(scene);
    const auto childText = root.FindChild("Part")->SaveToString();
    CHECK_FALSE(json::parse(childText)["entities"][0].contains("prefab"));
    auto roots = scene.Instantiate(childText);
    REQUIRE(roots.size() == 1);
    CHECK(roots[0]->GetPrefabRootId() == Spot::EntityId::Invalid);
    CHECK(roots[0]->GetComponent<PrefabData>()->target == Spot::EntityId::Invalid);
    const auto expanded = root.SaveToString(true);
    CHECK(json::parse(expanded)["entities"].size() == 4);
    Spot::Scene snapshot;
    REQUIRE(snapshot.LoadFromString(expanded));
    CHECK_FALSE(snapshot.FindEntity("Prop")->GetPrefabAssetId().IsValid());
    CHECK(snapshot.FindEntity("Part")->GetComponent<PrefabData>()->target == snapshot.FindEntity("Prop")->GetId());
}

TEST_CASE("Unavailable prefabs and malformed overrides reject a scene before changing its entities",
          "[scene][prefab][overrides]") {
    PrefabProject project;
    Spot::Scene source;
    project.Instantiate(source);
    json saved = json::parse(Spot::SaveSceneSource(source, project.assets.get()));
    SECTION("Unavailable asset") {
        saved["entities"][0]["prefab"] = Spot::AssetId::Generate().ToString();
    }
    SECTION("Bad id mapping") {
        saved["entities"][0]["entityIds"] = json::array();
    }
    SECTION("Bad overrides") {
        saved["entities"][0]["overrides"][std::to_string(static_cast<std::uint64_t>(project.rootId))] =
            json::array({{{"op", "move"}, {"path", "/name"}}});
    }
    SECTION("Removed root") {
        saved["entities"][0]["removedEntities"] = json::array({static_cast<std::uint64_t>(project.rootId)});
    }
    Spot::Scene scene;
    scene.CreateEntity("Existing");
    const auto before = scene.SaveToString();
    CHECK_FALSE(scene.LoadFromString(saved.dump(), project.assets.get()));
    CHECK(scene.SaveToString() == before);
}

TEST_CASE("Legacy prefab component ids normalize consistently and duplicate ids are rejected",
          "[assets][prefab][overrides]") {
    const std::string legacy = R"({"format":"spot-scene","version":1,"entities":[{"id":17,
        "components":[{"type":"PointLight","data":{}},{"type":"PointLight","data":{}}]}]})";
    const auto first = Spot::Prefab::FromString(legacy);
    const auto second = Spot::Prefab::FromString(legacy);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->GetEntityText() == second->GetEntityText());
    const auto saved = json::parse(first->GetEntityText());
    CHECK(saved["version"] == 2);
    CHECK(saved["entities"][0]["components"][0]["id"] != saved["entities"][0]["components"][1]["id"]);
    json invalid = saved;
    invalid["entities"][0]["components"][1]["id"] = invalid["entities"][0]["components"][0]["id"];
    CHECK_FALSE(Spot::Prefab::FromString(invalid.dump()));
}
