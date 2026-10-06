#include "scene/scene_commands.h"
#include "scene/scene_document.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using Spot::Entity;
using Spot::EntityId;
using Spot::Editor::SceneDocument;

namespace {

class Pointer final : public Spot::Component {
public:
    EntityId target = EntityId::Invalid;
    float value = 0.0f;

protected:
    void Serialize(Spot::Archive& archive) override {
        archive.Field("target", target);
        archive.Field("value", value);
    }
};

struct Fixture {
    SceneDocument document;
    Entity* root;
    Entity* child;
    Entity* other;

    Fixture() {
        Spot::RegisterComponent<Pointer>("Pointer");
        Spot::Scene& scene = document.GetScene();
        root = &scene.CreateEntity("Root");
        child = &root->CreateChild("Child");
        root->CreateChild("Second Child");
        other = &scene.CreateEntity("Other");
        other->GetTransform().SetPosition({1.0f, 2.0f, 3.0f});
        child->AddComponent<Pointer>().target = other->GetId();
        document.GetHistory().MarkSaved();
    }

    std::string Save() { return document.GetScene().SaveToString(); }

    // Executes the command, then checks that undo and redo bring back exactly the scenes before and after it.
    void CheckRoundTrip(std::unique_ptr<Spot::Editor::SceneCommand> command) {
        const std::string before = Save();
        const bool dirtyBefore = document.IsDirty();
        REQUIRE(document.Execute(std::move(command)));
        const std::string after = Save();
        CHECK(after != before);
        CHECK(document.IsDirty());

        document.Undo();
        CHECK(Save() == before);
        CHECK(document.IsDirty() == dirtyBefore);
        document.Redo();
        CHECK(Save() == after);
    }
};

std::vector<std::string> GetNames(const std::vector<Entity*>& entities) {
    std::vector<std::string> names;
    for (const Entity* entity : entities) {
        names.push_back(entity->GetName());
    }
    return names;
}

} // namespace

TEST_CASE("Deleted entities come back where they were, with their ids", "[editor][commands]") {
    Fixture fixture;
    const EntityId childId = fixture.child->GetId();
    const EntityId otherId = fixture.other->GetId();
    fixture.document.GetSelection().Set(std::vector{childId, otherId});

    fixture.CheckRoundTrip(
        std::make_unique<Spot::Editor::DeleteEntitiesCommand>(fixture.document, std::vector{childId, otherId}));

    CHECK_FALSE(fixture.document.GetScene().GetEntity(childId));
    fixture.document.Undo();
    // References between deleted entities come back too, and so does the selection.
    Entity* child = fixture.document.GetScene().GetEntity(childId);
    REQUIRE(child);
    CHECK(child->GetComponent<Pointer>()->target == otherId);
    CHECK(child->GetSiblingIndex() == 0);
    CHECK(fixture.document.GetSelection().Contains(otherId));
}

TEST_CASE("Created entities are selected and come back with the same ids", "[editor][commands]") {
    Fixture fixture;
    Entity* const copied[] = {fixture.child};
    const std::string text = fixture.document.GetScene().SaveEntitiesToString(copied);
    auto command = std::make_unique<Spot::Editor::CreateEntitiesCommand>(fixture.document, "Duplicate", text,
                                                                         fixture.root->GetId(), fixture.child->GetId());
    auto* created = command.get();

    fixture.CheckRoundTrip(std::move(command));

    REQUIRE(created->GetCreated().size() == 1);
    const EntityId copyId = created->GetCreated().front();
    CHECK(copyId != fixture.child->GetId());
    Entity* copy = fixture.document.GetScene().GetEntity(copyId);
    REQUIRE(copy);
    CHECK(copy->GetSiblingIndex() == 0);
    // A copy in the same scene keeps pointing at the same entity.
    CHECK(copy->GetComponent<Pointer>()->target == fixture.other->GetId());
    CHECK(fixture.document.GetSelection().GetPrimary() == copyId);
}

TEST_CASE("Reparenting keeps world poses and goes back to the old places", "[editor][commands]") {
    Fixture fixture;
    fixture.root->GetTransform().SetPosition({10.0f, 0.0f, 0.0f});
    const Spot::Vector3 world = fixture.other->GetTransform().GetWorldPosition();

    fixture.CheckRoundTrip(std::make_unique<Spot::Editor::ReparentEntitiesCommand>(
        fixture.document, std::vector{fixture.other->GetId()}, fixture.root->GetId(), fixture.child->GetId()));

    CHECK(GetNames(fixture.root->GetChildren()) == std::vector<std::string>{"Other", "Child", "Second Child"});
    CHECK(fixture.other->GetTransform().GetWorldPosition() == world);

    // Reordering among the same siblings, several at once.
    fixture.CheckRoundTrip(std::make_unique<Spot::Editor::ReparentEntitiesCommand>(
        fixture.document, std::vector{fixture.other->GetId(), fixture.child->GetId()}, fixture.root->GetId()));
    CHECK(GetNames(fixture.root->GetChildren()) == std::vector<std::string>{"Second Child", "Other", "Child"});
}

TEST_CASE("Entities cannot be moved below themselves", "[editor][commands]") {
    Fixture fixture;

    CHECK_FALSE(fixture.document.Execute(std::make_unique<Spot::Editor::ReparentEntitiesCommand>(
        fixture.document, std::vector{fixture.root->GetId()}, fixture.child->GetId())));

    CHECK(fixture.child->GetParent() == fixture.root);
    CHECK_FALSE(fixture.document.GetHistory().CanUndo());
}

TEST_CASE("Renaming, activating and moving entities undo", "[editor][commands]") {
    Fixture fixture;

    fixture.CheckRoundTrip(
        std::make_unique<Spot::Editor::RenameEntityCommand>(fixture.document, fixture.root->GetId(), "Renamed"));
    CHECK_FALSE(fixture.document.Execute(
        std::make_unique<Spot::Editor::RenameEntityCommand>(fixture.document, fixture.root->GetId(), "Renamed")));

    fixture.CheckRoundTrip(std::make_unique<Spot::Editor::SetEntitiesActiveCommand>(
        fixture.document, std::vector{fixture.root->GetId(), fixture.other->GetId()}, false));

    Spot::Editor::Pose moved = Spot::Editor::Pose::Of(fixture.other->GetTransform());
    moved.position.y = 8.0f;
    moved.scale = {2.0f, 2.0f, 2.0f};
    fixture.CheckRoundTrip(std::make_unique<Spot::Editor::SetTransformsCommand>(
        fixture.document, "Move",
        std::vector<Spot::Editor::SetTransformsCommand::Change>{
            {fixture.other->GetId(), Spot::Editor::Pose::Of(fixture.other->GetTransform()), moved}}));
}

TEST_CASE("Components are added, removed, moved and edited with undo", "[editor][commands]") {
    Fixture fixture;
    const EntityId id = fixture.child->GetId();

    fixture.CheckRoundTrip(std::make_unique<Spot::Editor::AddComponentCommand>(fixture.document, id, "PointLight"));
    CHECK(fixture.child->GetComponent<Spot::PointLight>());

    fixture.CheckRoundTrip(std::make_unique<Spot::Editor::MoveComponentCommand>(fixture.document, id, 1, 0));
    CHECK(fixture.child->GetComponentIndex(*fixture.child->GetComponent<Spot::PointLight>()) == 0);

    Pointer& pointer = *fixture.child->GetComponent<Pointer>();
    const std::string before = Spot::SaveComponentToString(pointer);
    pointer.value = 5.0f;
    const std::string after = Spot::SaveComponentToString(pointer);
    pointer.value = 0.0f;
    auto edit = std::make_unique<Spot::Editor::EditComponentCommand>(fixture.document, id, 1, before, after);
    CHECK(edit->GetName() == "Edit Pointer");
    fixture.CheckRoundTrip(std::move(edit));
    CHECK(fixture.child->GetComponent<Pointer>()->value == 5.0f);

    auto remove = std::make_unique<Spot::Editor::RemoveComponentCommand>(fixture.document, id, 1);
    CHECK(remove->GetName() == "Remove Pointer");
    fixture.CheckRoundTrip(std::move(remove));
    CHECK_FALSE(fixture.child->GetComponent<Pointer>());
    fixture.document.Undo();
    REQUIRE(fixture.child->GetComponent<Pointer>());
    CHECK(fixture.child->GetComponent<Pointer>()->target == fixture.other->GetId());
}

TEST_CASE("Scene documents save, and remember being saved", "[editor][commands]") {
    Fixture fixture;
    const auto file = std::filesystem::temp_directory_path() / "spot_editor_document_test.scene";
    REQUIRE(fixture.document.SaveAs(file));
    CHECK(fixture.document.GetName() == "spot_editor_document_test");

    fixture.document.Execute(
        std::make_unique<Spot::Editor::RenameEntityCommand>(fixture.document, fixture.root->GetId(), "Changed"));
    CHECK(fixture.document.IsDirty());
    REQUIRE(fixture.document.Save());
    CHECK_FALSE(fixture.document.IsDirty());

    const auto opened = SceneDocument::Open(file);
    std::filesystem::remove(file);
    REQUIRE(opened);
    CHECK(opened->GetScene().FindEntity("Changed"));
    CHECK_FALSE(opened->IsDirty());
    CHECK(opened->GetId() != fixture.document.GetId());
}
