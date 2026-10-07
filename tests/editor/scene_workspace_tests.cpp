#include "headless_imgui.h"
#include "temp_directory.h"

#include "app/editor_context.h"
#include "project/editor_project.h"
#include "project/project_templates.h"
#include "scene/scene_commands.h"
#include "scene/scene_workspace.h"
#include "viewport/editor_camera.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

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
        const auto file = Spot::Editor::CreateProject(temp.GetPath(), "Game", Spot::Editor::ProjectTemplate::Basic3D);
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

} // namespace

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
    // Entities get new ids when the scene loads again, and the same ones are selected.
    document = fixture.workspace->GetActiveDocument();
    const Spot::Entity* primary = document->GetScene().GetEntity(document->GetSelection().GetPrimary());
    REQUIRE(primary);
    CHECK(primary->GetName() == selected);
    // Opening an open scene brings it forward instead of opening it twice.
    fixture.workspace->OpenScene(fixture.workspace->GetActiveDocument()->GetAssetId());
    CHECK(fixture.workspace->GetDocumentCount() == 1);
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
