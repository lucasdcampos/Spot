#include "headless_imgui.h"
#include "temp_directory.h"

#include "app/editor_context.h"
#include "inspector/entity_inspector.h"
#include "project/editor_project.h"
#include "scene/prefab_actions.h"
#include "scene/scene_commands.h"
#include "scene/scene_workspace.h"
#include "viewport/editor_camera.h"
#include "viewport/scene_view.h"

#include <spot.h>
#include <spot_tools.h>

#include <ImGuizmo.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <utility>

using Catch::Approx;

namespace {

bool IsNear(Spot::Vector3 a, Spot::Vector3 b) {
    return Spot::Length(a - b) < 1e-3f;
}

// A project made from the 3D template, open in a workspace that draws headless.
struct WorkspaceFixture {
    TempDirectory temp;
    std::unique_ptr<Spot::Window> window = Spot::Window::Create();
    Spot::Tests::HeadlessImGui imgui;
    Spot::Editor::EditorSettings settings;
    std::unique_ptr<Spot::Editor::EditorContext> context;
    std::unique_ptr<Spot::Editor::SceneWorkspace> workspace;

    WorkspaceFixture() {
        REQUIRE(window != nullptr);
        const auto file = Spot::CreateProject(temp.GetPath(), "Game", Spot::ProjectTemplate::Basic3D);
        REQUIRE(file.has_value());
        context = std::make_unique<Spot::Editor::EditorContext>(*window, nullptr, settings);
        context->SetProject(Spot::Editor::EditorProject::Open(*file, nullptr));
        workspace = std::make_unique<Spot::Editor::SceneWorkspace>(*context);
        context->SetWorkspace(workspace.get());
    }

    ~WorkspaceFixture() { context->SetWorkspace(nullptr); }

    void Frame() {
        imgui.BeginFrame();
        workspace->Draw(ImGui::GetID("Scenes"));
        imgui.EndFrame();
    }

    void Press(ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key, true);
        Frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        Frame();
    }

    void Rename(Spot::Editor::SceneDocument& document) {
        Spot::Entity* sun = document.GetScene().FindEntity("Sun");
        REQUIRE(sun);
        REQUIRE(document.Execute(
            std::make_unique<Spot::Editor::RenameEntityCommand>(document, sun->GetId(), "Renamed Sun")));
    }
};

struct PrefabWorkspaceFixture : WorkspaceFixture {
    Spot::AssetId prefab;
    Spot::Editor::SceneDocument* sourceDocument = nullptr;
    Spot::Editor::SceneDocument* first = nullptr;
    Spot::Editor::SceneDocument* second = nullptr;
    Spot::EntityId firstRoot = Spot::EntityId::Invalid;
    Spot::EntityId secondRoot = Spot::EntityId::Invalid;
    Spot::ComponentId lightId;

    PrefabWorkspaceFixture() {
        Spot::Scene source;
        auto& root = source.CreateEntity("Lantern");
        auto& light = root.AddComponent<Spot::PointLight>();
        light.SetIntensity(2.0f);
        lightId = light.GetId();
        root.CreateChild("Bulb");
        const auto asset = Spot::Editor::CreatePrefab(Project(), root, "prefabs");
        REQUIRE(asset);
        prefab = *asset;
        sourceDocument = workspace->OpenScene(prefab);
        REQUIRE(sourceDocument);
        first = &workspace->NewScene();
        second = &workspace->NewScene();
        first->GetScene().Clear();
        second->GetScene().Clear();
        firstRoot = Instantiate(*first, prefab).GetId();
        secondRoot = Instantiate(*second, prefab).GetId();
        workspace->SyncWithAssets();
    }

    Spot::Editor::EditorProject& Project() { return *context->GetProject(); }
    Spot::Entity& Source() { return *sourceDocument->GetScene().GetRootEntities().front(); }
    Spot::Entity& First() { return *first->GetScene().GetEntity(firstRoot); }
    Spot::Entity& Second() { return *second->GetScene().GetEntity(secondRoot); }
    Spot::Entity& Instantiate(Spot::Editor::SceneDocument& document, Spot::AssetId asset) {
        auto* root = document.GetScene().Instantiate(Project().GetAssets().Load<Spot::Prefab>(asset),
                                                     {.assets = &Project().GetAssets()});
        REQUIRE(root);
        return *root;
    }
    Spot::PointLight& Light(Spot::Entity& entity) {
        const auto lights = entity.GetComponents<Spot::PointLight>();
        const auto found = std::ranges::find(lights, lightId, &Spot::Component::GetId);
        REQUIRE(found != lights.end());
        return **found;
    }
    void SaveSource() { workspace->Save(*sourceDocument); }
};

class WorkspaceReference final : public Spot::Component {
public:
    Spot::EntityId target = Spot::EntityId::Invalid;

protected:
    void Serialize(Spot::Archive& archive) override { archive.Field("target", target); }
};

} // namespace

TEST_CASE("Saving a prefab updates every open scene while retaining instance edits and history",
          "[editor][workspace][prefab]") {
    PrefabWorkspaceFixture fixture;
    const auto bulbId = fixture.First().FindChild("Bulb")->GetId();
    fixture.first->GetSelection().Select(bulbId);
    REQUIRE(fixture.first->Execute(
        std::make_unique<Spot::Editor::RenameEntityCommand>(*fixture.first, bulbId, "Custom Bulb")));
    const auto history = fixture.first->GetHistory().GetCurrentState();
    const auto secondHistory = fixture.second->GetHistory().GetCurrentState();
    fixture.First().CreateChild("Local Addition");
    fixture.Source().FindChild("Bulb")->SetName("Updated Bulb");
    fixture.Source().CreateChild("New Inherited Child");
    fixture.Light(fixture.Source()).SetIntensity(9.0f);
    fixture.SaveSource();

    CHECK(fixture.Light(fixture.First()).GetIntensity() == 9.0f);
    CHECK(fixture.Light(fixture.Second()).GetIntensity() == 9.0f);
    CHECK(fixture.First().FindChild("Custom Bulb"));
    CHECK(fixture.Second().FindChild("Updated Bulb"));
    CHECK(fixture.First().FindChild("Local Addition"));
    CHECK(fixture.First().FindChild("New Inherited Child"));
    CHECK(fixture.Second().FindChild("New Inherited Child"));
    CHECK(fixture.first->GetSelection().GetPrimary() == bulbId);
    CHECK(fixture.first->GetHistory().GetCurrentState() == history);
    CHECK(fixture.second->GetHistory().GetCurrentState() == secondHistory);
    CHECK_FALSE(fixture.sourceDocument->IsDirty());
    fixture.first->Undo();
    CHECK(fixture.first->GetScene().GetEntity(bulbId)->GetName() == "Bulb");
    CHECK(fixture.Light(fixture.First()).GetIntensity() == 9.0f);
    fixture.first->Redo();
    CHECK(fixture.First().FindChild("Custom Bulb"));
}

TEST_CASE("Component edit history follows UUIDs after prefab reordering and keeps newly inherited fields",
          "[editor][workspace][prefab]") {
    PrefabWorkspaceFixture fixture;
    auto& light = fixture.Light(fixture.First());
    const std::string before = Spot::SaveComponentToString(light);
    light.SetIntensity(8.0f);
    REQUIRE(fixture.first->Execute(std::make_unique<Spot::Editor::EditComponentCommand>(
        *fixture.first, fixture.firstRoot, 0, before, Spot::SaveComponentToString(light))));
    auto& source = fixture.Source();
    fixture.Light(source).SetRange(13.0f);
    auto& inserted = source.AddComponent<Spot::PointLight>();
    inserted.SetIntensity(32.0f);
    const auto insertedId = inserted.GetId();
    source.MoveComponent(inserted, 0);
    fixture.SaveSource();
    CHECK(fixture.Light(fixture.First()).GetIntensity() == 8.0f);
    CHECK(fixture.Light(fixture.First()).GetRange() == 13.0f);
    fixture.first->Undo();
    CHECK(fixture.Light(fixture.First()).GetIntensity() == 2.0f);
    CHECK(fixture.Light(fixture.First()).GetRange() == 13.0f);
    CHECK(fixture.First().GetComponents<Spot::PointLight>().front()->GetId() == insertedId);
    CHECK(fixture.First().GetComponents<Spot::PointLight>().front()->GetIntensity() == 32.0f);
    fixture.first->Redo();
    CHECK(fixture.Light(fixture.First()).GetIntensity() == 8.0f);
    CHECK(fixture.Light(fixture.First()).GetRange() == 13.0f);
}

TEST_CASE("Undoing an inherited entity deletion restores the prefab link and current source additions",
          "[editor][workspace][prefab]") {
    PrefabWorkspaceFixture fixture;
    const auto bulbId = fixture.First().FindChild("Bulb")->GetId();
    Spot::RegisterComponent<WorkspaceReference>("WorkspaceReference");
    auto& external = fixture.first->GetScene().CreateEntity("External");
    const auto externalId = external.GetId();
    external.AddComponent<WorkspaceReference>().target = bulbId;
    fixture.First().AddComponent<WorkspaceReference>().target = externalId;
    REQUIRE(fixture.first->Execute(
        std::make_unique<Spot::Editor::DeleteEntitiesCommand>(*fixture.first, std::vector{bulbId, externalId})));
    fixture.Source().CreateChild("Later Child");
    fixture.SaveSource();
    CHECK_FALSE(fixture.First().FindChild("Bulb"));
    fixture.first->Undo();
    auto* bulb = fixture.first->GetScene().GetEntity(bulbId);
    REQUIRE(bulb);
    CHECK(bulb->GetPrefabRootId() == fixture.firstRoot);
    CHECK(fixture.First().FindChild("Later Child"));
    CHECK(fixture.First().GetComponent<WorkspaceReference>()->target == externalId);
    REQUIRE(fixture.first->GetScene().GetEntity(externalId));
    CHECK(fixture.first->GetScene().GetEntity(externalId)->GetComponent<WorkspaceReference>()->target == bulbId);
    fixture.first->Redo();
    CHECK_FALSE(fixture.First().FindChild("Bulb"));
    CHECK(fixture.First().FindChild("Later Child"));
}

TEST_CASE("Prefab apply undo and redo propagate to other open documents without discarding their overrides",
          "[editor][workspace][prefab]") {
    PrefabWorkspaceFixture fixture;
    fixture.Second().SetName("Local Lantern");
    fixture.Light(fixture.First()).SetIntensity(8.0f);
    REQUIRE(fixture.first->Execute(std::make_unique<Spot::Editor::PrefabCommand>(
        *fixture.first, fixture.Project(), fixture.firstRoot, Spot::Editor::PrefabAction::Apply)));
    fixture.workspace->SyncWithAssets();
    CHECK(fixture.Light(fixture.Second()).GetIntensity() == 8.0f);
    CHECK(fixture.Light(fixture.Source()).GetIntensity() == 8.0f);
    CHECK(fixture.Second().GetName() == "Local Lantern");
    fixture.first->Undo();
    fixture.workspace->SyncWithAssets();
    CHECK(fixture.Light(fixture.Second()).GetIntensity() == 2.0f);
    CHECK(fixture.Light(fixture.Source()).GetIntensity() == 2.0f);
    CHECK(fixture.Light(fixture.First()).GetIntensity() == 8.0f);
    fixture.first->Redo();
    fixture.workspace->SyncWithAssets();
    CHECK(fixture.Light(fixture.Second()).GetIntensity() == 8.0f);
    CHECK(fixture.Second().GetName() == "Local Lantern");
}

TEST_CASE("Live updates preserve unsaved prefab authoring edits when another document applies overrides",
          "[editor][workspace][prefab]") {
    PrefabWorkspaceFixture fixture;
    const auto sourceId = fixture.Source().GetId();
    REQUIRE(fixture.sourceDocument->Execute(
        std::make_unique<Spot::Editor::RenameEntityCommand>(*fixture.sourceDocument, sourceId, "Unsaved Source Name")));
    const auto history = fixture.sourceDocument->GetHistory().GetCurrentState();
    fixture.Light(fixture.First()).SetIntensity(8.0f);
    REQUIRE(fixture.first->Execute(std::make_unique<Spot::Editor::PrefabCommand>(
        *fixture.first, fixture.Project(), fixture.firstRoot, Spot::Editor::PrefabAction::Apply)));
    fixture.workspace->SyncWithAssets();
    CHECK(fixture.Source().GetName() == "Unsaved Source Name");
    CHECK(fixture.sourceDocument->IsDirty());
    CHECK(fixture.sourceDocument->GetHistory().GetCurrentState() == history);
    fixture.sourceDocument->Undo();
    CHECK(fixture.Source().GetName() == "Lantern");
}

TEST_CASE("Invalid prefab dependencies leave open documents and their histories intact until repaired",
          "[editor][workspace][prefab]") {
    PrefabWorkspaceFixture fixture;
    REQUIRE(fixture.first->Execute(
        std::make_unique<Spot::Editor::RenameEntityCommand>(*fixture.first, fixture.firstRoot, "Custom Lantern")));
    const auto saved = fixture.first->GetScene().SaveToString();
    const auto history = fixture.first->GetHistory().GetCurrentState();
    const auto revision = fixture.first->GetSceneRevision();
    const std::string original = fixture.Source().SaveToString();
    // A source that nests itself does not import.
    std::ofstream(fixture.sourceDocument->GetFile(), std::ios::binary) << Spot::SaveEntitySource(fixture.First());
    CHECK_FALSE(fixture.Project().Reimport(fixture.prefab));
    fixture.workspace->SyncWithAssets();
    CHECK(fixture.first->GetScene().SaveToString() == saved);
    CHECK(fixture.first->GetHistory().GetCurrentState() == history);
    CHECK(fixture.first->GetSceneRevision() == revision);
    std::ofstream(fixture.sourceDocument->GetFile(), std::ios::binary) << original;
    REQUIRE(fixture.Project().Reimport(fixture.prefab));
    fixture.workspace->SyncWithAssets();
    CHECK(fixture.First().GetName() == "Custom Lantern");
    fixture.first->Undo();
    CHECK(fixture.First().GetName() == "Lantern");
}

TEST_CASE("Undo and redo tolerate prefab entities and components removed from the source",
          "[editor][workspace][prefab]") {
    PrefabWorkspaceFixture fixture;
    const auto bulb = fixture.First().FindChild("Bulb")->GetId();
    REQUIRE(fixture.first->Execute(std::make_unique<Spot::Editor::RenameEntityCommand>(*fixture.first, bulb, "Local")));
    REQUIRE(fixture.first->Execute(
        std::make_unique<Spot::Editor::SetEntitiesActiveCommand>(*fixture.first, std::vector{bulb}, false)));
    REQUIRE(fixture.first->Execute(
        std::make_unique<Spot::Editor::SetEntitiesLayerCommand>(*fixture.first, std::vector{bulb}, 3)));
    REQUIRE(fixture.first->Execute(
        std::make_unique<Spot::Editor::SetEntitiesTagCommand>(*fixture.first, std::vector{bulb}, "Local", true)));
    REQUIRE(fixture.first->Execute(
        std::make_unique<Spot::Editor::AddComponentCommand>(*fixture.first, bulb, "PointLight")));
    const auto before = Spot::SaveComponentToString(fixture.Light(fixture.First()));
    fixture.Light(fixture.First()).SetIntensity(8.0f);
    REQUIRE(fixture.first->Execute(std::make_unique<Spot::Editor::EditComponentCommand>(
        *fixture.first, fixture.firstRoot, 0, before, Spot::SaveComponentToString(fixture.Light(fixture.First())))));
    fixture.Source().FindChild("Bulb")->Destroy();
    fixture.Source().RemoveComponent(fixture.Light(fixture.Source()));
    fixture.SaveSource();
    CHECK_FALSE(fixture.first->GetScene().GetEntity(bulb));
    CHECK(fixture.First().GetComponents<Spot::PointLight>().empty());
    for (int i = 0; i < 6; ++i) {
        fixture.first->Undo();
    }
    for (int i = 0; i < 6; ++i) {
        fixture.first->Redo();
    }
    CHECK_FALSE(fixture.first->GetScene().GetEntity(bulb));
    CHECK(fixture.First().GetComponents<Spot::PointLight>().empty());
    CHECK(fixture.First().GetName() == "Lantern");
}

TEST_CASE("Live prefab updates reach nested and variant documents and their scene instances",
          "[editor][workspace][prefab]") {
    PrefabWorkspaceFixture fixture;
    fixture.First().SetName("Variant");
    const auto variant = Spot::Editor::CreatePrefab(fixture.Project(), fixture.First(), "prefabs");
    REQUIRE(variant);
    Spot::Scene assembly;
    auto& wrapper = assembly.CreateEntity("Assembly");
    auto* nested = assembly.Instantiate(fixture.Project().GetAssets().Load<Spot::Prefab>(*variant),
                                        {.parent = &wrapper, .assets = &fixture.Project().GetAssets()});
    REQUIRE(nested);
    const auto containing = Spot::Editor::CreatePrefab(fixture.Project(), wrapper, "prefabs");
    REQUIRE(containing);
    auto* variantDocument = fixture.workspace->OpenScene(*variant);
    auto* containingDocument = fixture.workspace->OpenScene(*containing);
    REQUIRE(variantDocument);
    REQUIRE(containingDocument);
    const auto instanceId = fixture.Instantiate(*fixture.second, *containing).GetId();
    fixture.workspace->SyncWithAssets();
    fixture.Light(fixture.Source()).SetIntensity(17.0f);
    fixture.Source().CreateChild("Inherited Addition");
    fixture.SaveSource();
    auto& variantRoot = *variantDocument->GetScene().GetRootEntities().front();
    auto* containingRoot = containingDocument->GetScene().FindEntity("Variant");
    auto* runtime = fixture.second->GetScene().GetEntity(instanceId)->FindChild("Variant");
    REQUIRE(containingRoot);
    REQUIRE(runtime);
    CHECK(variantRoot.GetName() == "Variant");
    CHECK(fixture.Light(variantRoot).GetIntensity() == 17.0f);
    CHECK(fixture.Light(*containingRoot).GetIntensity() == 17.0f);
    CHECK(fixture.Light(*runtime).GetIntensity() == 17.0f);
    CHECK(runtime->FindChild("Inherited Addition"));
    CHECK(variantRoot.FindChild("Inherited Addition"));
    CHECK(containingRoot->FindChild("Inherited Addition"));
}

TEST_CASE("Component addition and removal history retains identity through live prefab structural changes",
          "[editor][workspace][prefab]") {
    PrefabWorkspaceFixture fixture;
    SECTION("Undoing a local addition removes its UUID after an inherited component disappears") {
        REQUIRE(fixture.first->Execute(
            std::make_unique<Spot::Editor::AddComponentCommand>(*fixture.first, fixture.firstRoot, "PointLight")));
        const auto addedId = fixture.First().GetComponents<Spot::PointLight>().back()->GetId();
        fixture.Source().RemoveComponent(fixture.Light(fixture.Source()));
        const auto newSourceId = fixture.Source().AddComponent<Spot::PointLight>().GetId();
        fixture.SaveSource();
        fixture.first->Undo();
        const auto remaining = fixture.First().GetComponents<Spot::PointLight>();
        REQUIRE(remaining.size() == 1);
        CHECK(remaining.front()->GetId() == newSourceId);
        fixture.first->Redo();
        const auto restored = fixture.First().GetComponents<Spot::PointLight>();
        CHECK(std::ranges::find(restored, addedId, &Spot::Component::GetId) != restored.end());
        CHECK(std::ranges::find(restored, newSourceId, &Spot::Component::GetId) != restored.end());
    }
    SECTION("Redoing a removal removes the old component and leaves newly inherited ones") {
        REQUIRE(fixture.first->Execute(
            std::make_unique<Spot::Editor::RemoveComponentCommand>(*fixture.first, fixture.firstRoot, 0)));
        const auto newSourceId = fixture.Source().AddComponent<Spot::PointLight>().GetId();
        fixture.SaveSource();
        fixture.first->Undo();
        CHECK(fixture.Light(fixture.First()).GetIntensity() == 2.0f);
        fixture.first->Redo();
        const auto remaining = fixture.First().GetComponents<Spot::PointLight>();
        REQUIRE(remaining.size() == 1);
        CHECK(remaining.front()->GetId() == newSourceId);
    }
}

TEST_CASE("Inspector mixed values refresh after prefab updates without a new history entry",
          "[editor][workspace][prefab][ui]") {
    PrefabWorkspaceFixture fixture;
    const auto otherId = fixture.Instantiate(*fixture.first, fixture.prefab).GetId();
    fixture.Light(*fixture.first->GetScene().GetEntity(otherId)).SetIntensity(8.0f);
    fixture.first->GetSelection().Set(std::vector{fixture.firstRoot, otherId});
    Spot::Editor::EntityInspector inspector(*fixture.context);
    const auto draw = [&] {
        fixture.imgui.BeginFrame();
        ImGui::Begin("Inspector");
        inspector.Draw(*fixture.first);
        ImGui::End();
        fixture.imgui.EndFrame();
    };
    draw();
    CHECK(inspector.GetMixedValues(*fixture.first, "PointLight") == std::vector<std::string>{"/data/intensity"});
    const auto history = fixture.first->GetHistory().GetCurrentState();
    fixture.Light(fixture.Source()).SetIntensity(8.0f);
    fixture.SaveSource();
    draw();
    CHECK(fixture.first->GetHistory().GetCurrentState() == history);
    CHECK(inspector.GetMixedValues(*fixture.first, "PointLight").empty());
}

TEST_CASE("The editor camera looks at its pivot and turns around it", "[editor][camera]") {
    Spot::Editor::EditorCamera camera;
    camera.SetView({1.0f, 0.0f, 2.0f}, 0.0f, 0.0f, 5.0f);

    CHECK(IsNear(camera.GetPosition(), {1.0f, 0.0f, 7.0f}));
    CHECK(IsNear(camera.GetCamera().GetForward(), {0.0f, 0.0f, -1.0f}));

    camera.Update({.mouseDelta = {-100.0f, 0.0f}, .orbit = true});
    CHECK(camera.GetPivot() == Spot::Vector3{1.0f, 0.0f, 2.0f});
    CHECK(camera.GetDistance() == Approx(5.0f));
    CHECK(camera.GetYaw() == Approx(0.4f));

    camera.Update({.wheel = 1.0f});
    CHECK(camera.GetDistance() < 5.0f);
}

TEST_CASE("Flying turns the eye in place and moves it with the keys", "[editor][camera]") {
    Spot::Editor::EditorCamera camera;
    camera.SetView({}, 0.0f, 0.0f, 5.0f);
    camera.SetSpeed(2.0f);
    const Spot::Vector3 eye = camera.GetPosition();

    camera.Update({.mouseDelta = {50.0f, 30.0f}, .look = true});
    CHECK(IsNear(camera.GetPosition(), eye));
    CHECK(camera.GetPitch() < 0.0f);

    camera.SetView({}, 0.0f, 0.0f, 5.0f);
    camera.Update({.look = true, .move = {0.0f, 0.0f, 1.0f}, .deltaTime = 0.5f});
    CHECK(IsNear(camera.GetPosition(), {0.0f, 0.0f, 4.0f}));
    camera.Update({.look = true, .move = {1.0f, 0.0f, 0.0f}, .fast = true, .deltaTime = 0.5f});
    CHECK(IsNear(camera.GetPosition(), {3.0f, 0.0f, 4.0f}));

    // The wheel changes the speed while flying, instead of zooming.
    camera.Update({.wheel = 2.0f, .look = true});
    CHECK(camera.GetSpeed() > 2.0f);
    CHECK(camera.GetDistance() == Approx(5.0f));
}

TEST_CASE("2D views look at the XY plane, slide with the mouse and zoom toward it", "[editor][camera]") {
    Spot::Editor::EditorCamera camera;
    camera.SetView({1.0f, 2.0f, 3.0f}, 0.5f, -0.25f, 7.0f);
    camera.Set2D(true);
    camera.SetCenter2D({0.0f, 0.0f});
    camera.SetSize2D(5.0f);
    const Spot::Camera3D view = camera.GetCamera();
    CHECK(view.orthographic);
    CHECK(view.orthographicSize == 5.0f);
    CHECK(view.rotation == Spot::Quaternion::Identity());
    CHECK(view.position.x == 0.0f);
    CHECK(view.position.z > 0.0f);
    CHECK(camera.GetFocus() == Spot::Vector3{0.0f, 0.0f, 0.0f});

    // The plane follows the mouse: 100 pixels of a 100 pixel tall view are 10 units.
    camera.Update({.mouseDelta = {100.0f, 0.0f}, .pan = true, .viewHeight = 100.0f});
    CHECK(camera.GetCenter2D().x == Approx(-10.0f));
    camera.Update({.mouseDelta = {0.0f, 50.0f}, .look = true, .viewHeight = 100.0f});
    CHECK(camera.GetCenter2D().y == Approx(5.0f));

    // Zooming keeps the point under the mouse in place.
    camera.SetCenter2D({0.0f, 0.0f});
    const Spot::Vector2 cursor{25.0f, -25.0f};
    const auto under = [&] {
        const float unitsPerPixel = 2.0f * camera.GetSize2D() / 100.0f;
        return camera.GetCenter2D() + Spot::Vector2{cursor.x, -cursor.y} * unitsPerPixel;
    };
    const Spot::Vector2 before = under();
    camera.Update({.wheel = 2.0f, .viewHeight = 100.0f, .cursor = cursor});
    CHECK(camera.GetSize2D() < 5.0f);
    CHECK(under().x == Approx(before.x));
    CHECK(under().y == Approx(before.y));

    camera.Frame({3.0f, 4.0f, 9.0f}, 2.0f);
    CHECK(camera.GetCenter2D() == Spot::Vector2{3.0f, 4.0f});
    CHECK(camera.GetSize2D() == Approx(2.2f));
    // The 3D view stays as it was.
    camera.Set2D(false);
    CHECK(camera.GetPivot() == Spot::Vector3{1.0f, 2.0f, 3.0f});
    CHECK(camera.GetDistance() == Approx(7.0f));
}

TEST_CASE("Panning slides the pivot and framing fits a sphere", "[editor][camera]") {
    Spot::Editor::EditorCamera camera;
    camera.SetView({}, 0.0f, 0.0f, 5.0f);

    camera.Update({.mouseDelta = {10.0f, 0.0f}, .pan = true, .viewHeight = 100.0f});
    CHECK(camera.GetPivot().x < 0.0f);
    CHECK(camera.GetPivot().y == Approx(0.0f).margin(1e-5));

    camera.Frame({3.0f, 1.0f, -2.0f}, 1.0f);
    CHECK(camera.GetPivot() == Spot::Vector3{3.0f, 1.0f, -2.0f});
    CHECK(camera.GetDistance() > 2.0f);
    CHECK(camera.GetDistance() < 3.0f);
}

TEST_CASE("The workspace opens the project's startup scene and remembers its views", "[editor][workspace]") {
    WorkspaceFixture fixture;

    fixture.workspace->RestoreState();
    REQUIRE(fixture.workspace->GetDocumentCount() == 1);
    Spot::Editor::SceneDocument* document = fixture.workspace->GetActiveDocument();
    REQUIRE(document);
    CHECK(document->GetName() == "Main");
    CHECK_FALSE(document->IsDirty());
    fixture.Frame();
    fixture.workspace->GetActiveView()->GetCamera().SetView({1.0f, 2.0f, 3.0f}, 0.5f, -0.25f, 7.0f);
    fixture.workspace->GetActiveView()->GetCamera().Set2D(true);
    fixture.workspace->GetActiveView()->GetCamera().SetCenter2D({4.0f, 5.0f});
    const std::vector<Spot::Entity*> roots = document->GetScene().GetRootEntities();
    REQUIRE(roots.size() >= 2);
    const std::string selected = roots[1]->GetName();
    document->GetSelection().Select(roots[1]->GetId());

    fixture.workspace->SaveState();
    fixture.workspace->CloseAll();
    CHECK(fixture.workspace->GetDocumentCount() == 0);
    fixture.workspace->RestoreState();

    REQUIRE(fixture.workspace->GetDocumentCount() == 1);
    const Spot::Editor::EditorCamera& camera = fixture.workspace->GetActiveView()->GetCamera();
    CHECK(camera.GetPivot() == Spot::Vector3{1.0f, 2.0f, 3.0f});
    CHECK(camera.GetYaw() == Approx(0.5f));
    CHECK(camera.GetDistance() == Approx(7.0f));
    CHECK(camera.Is2D());
    CHECK(camera.GetCenter2D() == Spot::Vector2{4.0f, 5.0f});
    // Entities get new ids when the scene loads again, and the same ones are selected.
    document = fixture.workspace->GetActiveDocument();
    const Spot::Entity* primary = document->GetScene().GetEntity(document->GetSelection().GetPrimary());
    REQUIRE(primary);
    CHECK(primary->GetName() == selected);
    // Opening an open scene brings it forward instead of opening it twice.
    fixture.workspace->OpenScene(fixture.workspace->GetActiveDocument()->GetAssetId());
    CHECK(fixture.workspace->GetDocumentCount() == 1);
}

TEST_CASE("Clicking an icon in a view selects its entity, and Ctrl adds to the selection", "[editor][workspace][ui]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    Spot::Entity* sun = document.GetScene().FindEntity("Sun");
    Spot::Entity* sky = document.GetScene().FindEntity("Sky");
    REQUIRE((sun && sky));
    // Apart, so neither icon covers the other.
    sun->GetTransform().SetPosition({2.0f, 0.0f, 0.0f});
    sky->GetTransform().SetPosition({-2.0f, 0.0f, 0.0f});
    fixture.imgui.SetDraw([&] {
        ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({800.0f, 600.0f});
        fixture.workspace->Draw(ImGui::GetID("Scenes"));
    });
    fixture.imgui.Frame();
    fixture.imgui.Frame();

    const auto center = [&](const Spot::Entity& entity) {
        const auto rect = fixture.workspace->GetActiveView()->GetIconRect(entity.GetId());
        REQUIRE(rect.has_value());
        return ImVec2{(rect->first.x + rect->second.x) * 0.5f, (rect->first.y + rect->second.y) * 0.5f};
    };
    fixture.imgui.Click(center(*sun));
    CHECK(document.GetSelection().GetPrimary() == sun->GetId());
    CHECK(document.GetSelection().GetEntities().size() == 1);

    fixture.imgui.Click(center(*sky), ImGuiMod_Ctrl);
    CHECK(document.GetSelection().Contains(sun->GetId()));
    CHECK(document.GetSelection().GetPrimary() == sky->GetId());
}

TEST_CASE("Dragging the gizmo moves the selection as one undoable step", "[editor][workspace][gizmo][ui]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    Spot::Entity* sun = document.GetScene().FindEntity("Sun");
    REQUIRE(sun);
    sun->GetTransform().SetPosition({2.0f, 0.0f, 0.0f});
    document.GetSelection().Select(sun->GetId());
    fixture.settings.GetGizmo().tool = Spot::Editor::GizmoTool::Move;
    fixture.imgui.SetDraw([&] {
        ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({800.0f, 600.0f});
        fixture.workspace->Draw(ImGui::GetID("Scenes"));
    });
    fixture.imgui.Frame();
    fixture.imgui.Frame();

    // The gizmo's center, over the sun's icon, moves it in the plane of the view.
    const auto rect = fixture.workspace->GetActiveView()->GetIconRect(sun->GetId());
    REQUIRE(rect.has_value());
    const ImVec2 center{(rect->first.x + rect->second.x) * 0.5f, (rect->first.y + rect->second.y) * 0.5f};
    const std::size_t steps = document.GetHistory().GetUndoCount();
    fixture.imgui.Drag(center, {center.x + 80.0f, center.y});

    const Spot::Vector3 moved = sun->GetTransform().GetPosition();
    CHECK_FALSE(IsNear(moved, {2.0f, 0.0f, 0.0f}));
    // Still the only entity selected: the click went to the gizmo, not to picking.
    CHECK(document.GetSelection().GetPrimary() == sun->GetId());
    CHECK(document.GetHistory().GetUndoCount() == steps + 1);
    CHECK(document.GetHistory().GetUndoName() == "Move");
    document.Undo();
    CHECK(IsNear(sun->GetTransform().GetPosition(), {2.0f, 0.0f, 0.0f}));
}

TEST_CASE("The gizmo of a docked view still moves the selection after the scene played",
          "[editor][workspace][gizmo][play][ui]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    Spot::Entity* sun = document.GetScene().FindEntity("Sun");
    REQUIRE(sun);
    sun->GetTransform().SetPosition({2.0f, 0.0f, 0.0f});
    document.GetSelection().Select(sun->GetId());
    fixture.settings.GetGizmo().tool = Spot::Editor::GizmoTool::Move;
    // Docked, the view's window takes the longer title it has while playing in a new buffer.
    fixture.imgui.SetDraw([&] {
        ImGuizmo::BeginFrame();
        const ImGuiID dockSpace = ImGui::GetID("DockSpace");
        ImGui::DockSpaceOverViewport(dockSpace, ImGui::GetMainViewport());
        fixture.workspace->Draw(dockSpace);
    });
    fixture.imgui.Frame();
    fixture.imgui.Frame();

    class PlayApplication final : public Spot::Editor::PlayHost {
    public:
        explicit PlayApplication(std::function<void(Spot::Editor::PlayHost&)> frame)
            : PlayHost(
                  {.window = {.title = "Play Tests", .width = 320, .height = 240}, .graphics = {.enabled = false}}),
              m_frame(std::move(frame)) {}

    protected:
        void OnUpdate(float /*deltaTime*/) override {
            m_frame(*this);
            Quit();
        }

    private:
        std::function<void(Spot::Editor::PlayHost&)> m_frame;
    };
    PlayApplication application([&](Spot::Editor::PlayHost& app) {
        fixture.workspace->Play(app);
        fixture.imgui.Frame();
        fixture.workspace->StopPlaying();
    });
    REQUIRE(application.Run() == EXIT_SUCCESS);
    fixture.imgui.Frame();
    fixture.imgui.Frame();

    const auto rect = fixture.workspace->GetActiveView()->GetIconRect(sun->GetId());
    REQUIRE(rect.has_value());
    const ImVec2 center{(rect->first.x + rect->second.x) * 0.5f, (rect->first.y + rect->second.y) * 0.5f};
    fixture.imgui.Drag(center, {center.x + 80.0f, center.y});

    CHECK_FALSE(IsNear(sun->GetTransform().GetPosition(), {2.0f, 0.0f, 0.0f}));
    CHECK(document.GetSelection().GetPrimary() == sun->GetId());
}

TEST_CASE("Open scenes show the new versions of assets that changed", "[editor][workspace]") {
    WorkspaceFixture fixture;
    Spot::Editor::EditorProject& project = *fixture.context->GetProject();
    const auto file = project.GetProject().GetAssetsDirectory() / "paint.material";
    REQUIRE(Spot::MaterialSource{.baseColor = Spot::Color{1.0f, 0.0f, 0.0f, 1.0f}}.SaveToFile(file));
    project.Refresh();
    const Spot::AssetId paint = *project.GetAssetDatabase().FindByPath("paint.material");
    fixture.workspace->RestoreState();
    fixture.Frame();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    Spot::Entity* cube = document.GetScene().FindEntity("Cube");
    REQUIRE(cube);
    auto& renderer = *cube->GetComponent<Spot::MeshRenderer>();
    renderer.SetMaterial(project.GetAssets().Load<Spot::Material>(paint));

    // Edited in another application, such as a text editor.
    REQUIRE(Spot::MaterialSource{.baseColor = Spot::Color{0.0f, 1.0f, 0.0f, 1.0f}}.SaveToFile(file));
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::hours(1));
    project.Refresh();
    fixture.Frame();
    REQUIRE(renderer.GetMaterial());
    CHECK(renderer.GetMaterial()->baseColor.g == 1.0f);
    CHECK_FALSE(document.IsDirty());
}

TEST_CASE("Closing a changed scene asks first, and Escape keeps it open", "[editor][workspace]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    fixture.Rename(document);
    REQUIRE(document.IsDirty());

    fixture.workspace->Close(document);
    fixture.Frame();
    CHECK(fixture.workspace->GetDocumentCount() == 1);
    fixture.Press(ImGuiKey_Escape);

    CHECK(fixture.workspace->GetDocumentCount() == 1);
    CHECK(document.IsDirty());
}

TEST_CASE("Saving when asked writes the scene and then goes on", "[editor][workspace]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    const std::filesystem::path file = document.GetFile();
    fixture.Rename(document);
    bool proceeded = false;

    fixture.workspace->ConfirmAllUnsavedChanges([&] { proceeded = true; });
    fixture.Frame();
    CHECK_FALSE(proceeded);
    fixture.Press(ImGuiKey_Enter);

    CHECK(proceeded);
    CHECK_FALSE(document.IsDirty());
    Spot::Scene saved;
    REQUIRE(saved.LoadFromFile(file));
    CHECK(saved.FindEntity("Renamed Sun"));
}

TEST_CASE("Unchanged scenes close without asking, and new ones count as changed", "[editor][workspace]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    bool proceeded = false;

    fixture.workspace->ConfirmAllUnsavedChanges([&] { proceeded = true; });
    CHECK(proceeded);

    fixture.workspace->Close(*fixture.workspace->GetActiveDocument());
    CHECK(fixture.workspace->GetDocumentCount() == 0);

    Spot::Editor::SceneDocument& created = fixture.workspace->NewScene();
    CHECK(created.IsDirty());
    CHECK(created.GetScene().FindComponent<Spot::Sky>());
    CHECK(fixture.workspace->GetActiveDocument() == &created);
    CHECK(fixture.workspace->HasUnsavedChanges());
    fixture.Frame();
}

TEST_CASE("Reloading the scenes makes their components anew, and keeps ids, selections and histories",
          "[editor][workspace]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    fixture.Frame();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    Spot::Entity* cube = document.GetScene().FindEntity("Cube");
    REQUIRE(cube);
    const Spot::EntityId cubeId = cube->GetId();
    const std::size_t entityCount = document.GetScene().GetEntityCount();
    document.GetSelection().Select(cubeId);
    fixture.Rename(document);
    const std::string before = document.GetScene().SaveToString();

    bool emptied = false;
    fixture.workspace->ReloadScenes([&] { emptied = document.GetScene().GetEntityCount() == 0; });

    // Between the two, as when the game's code is replaced, no component is left.
    CHECK(emptied);
    CHECK(document.GetScene().GetEntityCount() == entityCount);
    CHECK(document.GetScene().SaveToString() == before);
    REQUIRE(document.GetScene().GetEntity(cubeId));
    CHECK(document.GetScene().GetEntity(cubeId)->GetComponent<Spot::MeshRenderer>());
    CHECK(document.GetSelection().GetPrimary() == cubeId);
    CHECK(document.IsDirty());
    document.Undo();
    CHECK(document.GetScene().FindEntity("Sun"));
    CHECK_FALSE(document.IsDirty());
}

TEST_CASE("Views show their scene again as soon as it stops playing", "[editor][workspace][play]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    fixture.Frame();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    Spot::Editor::SceneView& view = *fixture.workspace->GetActiveView();
    bool showedCopy = false;
    bool showedDocument = false;

    // Playing needs the application running.
    class PlayApplication final : public Spot::Editor::PlayHost {
    public:
        explicit PlayApplication(std::function<void(Spot::Editor::PlayHost&)> frame)
            : PlayHost(
                  {.window = {.title = "Play Tests", .width = 320, .height = 240}, .graphics = {.enabled = false}}),
              m_frame(std::move(frame)) {}

    protected:
        void OnUpdate(float /*deltaTime*/) override {
            m_frame(*this);
            Quit();
        }

    private:
        std::function<void(Spot::Editor::PlayHost&)> m_frame;
    };
    PlayApplication application([&](Spot::Editor::PlayHost& app) {
        fixture.workspace->Play(app);
        fixture.Frame();
        showedCopy = &view.GetShownDocument() != &document && view.IsShowingGame();
        // As with Ctrl+P, stopping after the view was drawn: it renders before it is drawn again.
        fixture.workspace->StopPlaying();
        showedDocument = &view.GetShownDocument() == &document && !view.IsShowingGame();
    });
    REQUIRE(application.Run() == EXIT_SUCCESS);

    CHECK(showedCopy);
    CHECK(showedDocument);
}

TEST_CASE("A scene waiting for the game's code to play says so in its view until it is cancelled",
          "[editor][workspace][play]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    fixture.Frame();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    const std::string before = document.GetScene().SaveToString();

    fixture.workspace->SetPlayPending(true);
    fixture.Frame();
    fixture.Frame();
    // Waiting plays nothing yet, and leaves the scene as it was.
    CHECK(fixture.workspace->IsPlayPending());
    CHECK_FALSE(fixture.workspace->GetPlaySession());
    CHECK(document.GetScene().SaveToString() == before);
    CHECK_FALSE(document.IsDirty());

    fixture.workspace->StopPlaying();
    CHECK_FALSE(fixture.workspace->IsPlayPending());
    fixture.Frame();
}

TEST_CASE("Scenes with unsaved changes are backed up until they are saved", "[editor][workspace][backup]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();

    // Nothing to keep while the scene is as saved.
    fixture.workspace->BackUp();
    CHECK(fixture.workspace->FindBackups().empty());

    fixture.Rename(document);
    fixture.workspace->BackUp();
    const std::vector<Spot::Editor::SceneBackup> backups = fixture.workspace->FindBackups();
    REQUIRE(backups.size() == 1);
    CHECK(backups[0].asset == document.GetAssetId());
    CHECK(backups[0].name == "Main");
    CHECK(backups[0].scene == document.GetScene().SaveToString());

    fixture.workspace->Save(document);
    CHECK(fixture.workspace->FindBackups().empty());
}

TEST_CASE("Backups left behind bring the changes back as a step to undo", "[editor][workspace][backup]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    Spot::Editor::SceneDocument& document = *fixture.workspace->GetActiveDocument();
    fixture.Rename(document);
    Spot::Editor::SceneDocument& untitled = fixture.workspace->NewScene();
    untitled.GetScene().CreateEntity("Only Here");
    fixture.workspace->BackUp();

    // Another editor, as after this one stopped without saving, finds what it left.
    Spot::Editor::SceneWorkspace next(*fixture.context);
    next.RestoreState();
    REQUIRE(next.FindBackups().size() == 2);
    next.RecoverBackups();

    CHECK(next.FindBackups().empty());
    Spot::Editor::SceneDocument* main = nullptr;
    Spot::Editor::SceneDocument* recovered = nullptr;
    for (std::size_t i = 0; i < next.GetDocumentCount(); ++i) {
        Spot::Editor::SceneDocument& candidate = next.GetDocument(i);
        (candidate.GetAssetId().IsValid() ? main : recovered) = &candidate;
    }
    REQUIRE(main);
    REQUIRE(recovered);
    CHECK(main->GetScene().FindEntity("Renamed Sun"));
    CHECK(main->IsDirty());
    main->Undo();
    CHECK(main->GetScene().FindEntity("Sun"));
    CHECK_FALSE(main->IsDirty());
    CHECK(recovered->GetName() == "Untitled");
    CHECK(recovered->GetScene().FindEntity("Only Here"));
}

TEST_CASE("Closing a scene drops its backup", "[editor][workspace][backup]") {
    WorkspaceFixture fixture;
    fixture.workspace->RestoreState();
    fixture.Rename(*fixture.workspace->GetActiveDocument());
    fixture.workspace->BackUp();
    REQUIRE(fixture.workspace->FindBackups().size() == 1);

    fixture.workspace->CloseAll();

    CHECK(fixture.workspace->FindBackups().empty());
}
