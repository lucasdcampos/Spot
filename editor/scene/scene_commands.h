#pragma once

#include "scene/scene_document.h"

#include <spot.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Spot::Editor {

// Adds saved entities, such as pasted, duplicated or new ones, under a parent before one of its children, and
// selects them. Redoing brings back the same entities, with the same ids.
class CreateEntitiesCommand final : public SceneCommand {
public:
    // Where saved entities go. The text is in the format of Scene::SaveToString(). Without a parent they become
    // roots; without an entity to go before, they go after the others.
    struct Placement {
        std::string text = {};
        EntityId parent = EntityId::Invalid;
        EntityId before = EntityId::Invalid;
    };

    CreateEntitiesCommand(SceneDocument& document, std::string name, std::string text,
                          EntityId parent = EntityId::Invalid, EntityId before = EntityId::Invalid);
    // Several groups of entities in different places, created as one step, such as duplicates of entities with
    // different parents.
    CreateEntitiesCommand(SceneDocument& document, std::string name, std::vector<Placement> placements);

    std::string GetName() const override { return m_name; }
    bool Execute() override;
    void Undo() override;
    std::optional<std::vector<EntityId>> GetSelectionAfter() const override { return m_created; }

    const std::vector<EntityId>& GetCreated() const { return m_created; }

private:
    std::string m_name;
    std::vector<Placement> m_placements;
    std::vector<EntityId> m_created;
    bool m_executed = false;
};

// Deletes entities with their descendants; undoing brings them back where they were, with their ids, so references
// to them work again.
class DeleteEntitiesCommand final : public SceneCommand {
public:
    DeleteEntitiesCommand(SceneDocument& document, std::vector<EntityId> entities);

    std::string GetName() const override;
    bool Execute() override;
    void Undo() override;
    std::optional<std::vector<EntityId>> GetSelectionAfter() const override { return std::vector<EntityId>(); }

private:
    struct Deleted {
        EntityId parent;
        std::size_t index;
        std::string text;
    };

    std::vector<EntityId> m_entities;
    // In hierarchy order, so each goes back to a place that already exists.
    std::vector<Deleted> m_deleted;
};

// Moves entities under a new parent, or to the root, before one of its children or after them all, keeping where
// they are in the world.
class ReparentEntitiesCommand final : public SceneCommand {
public:
    ReparentEntitiesCommand(SceneDocument& document, std::vector<EntityId> entities, EntityId parent,
                            EntityId before = EntityId::Invalid);

    std::string GetName() const override { return "Move in Hierarchy"; }
    bool Execute() override;
    void Undo() override;

private:
    struct Previous {
        EntityId entity;
        EntityId parent;
        std::size_t index;
        Vector3 position;
        Quaternion rotation;
        Vector3 scale;
    };

    std::vector<EntityId> m_entities;
    EntityId m_parent;
    EntityId m_before;
    // In hierarchy order, so each goes back to a place that already exists.
    std::vector<Previous> m_previous;
};

class RenameEntityCommand final : public SceneCommand {
public:
    RenameEntityCommand(SceneDocument& document, EntityId entity, std::string name);

    std::string GetName() const override { return "Rename Entity"; }
    bool Execute() override;
    void Undo() override;

private:
    EntityId m_entity;
    std::string m_name;
    std::string m_previous;
};

class SetEntitiesActiveCommand final : public SceneCommand {
public:
    SetEntitiesActiveCommand(SceneDocument& document, std::vector<EntityId> entities, bool active);

    std::string GetName() const override { return m_active ? "Activate" : "Deactivate"; }
    bool Execute() override;
    void Undo() override;

private:
    std::vector<EntityId> m_entities;
    bool m_active;
    std::vector<EntityId> m_changed;
};

// A transform's local position, rotation and scale.
struct Pose {
    Vector3 position = {};
    Quaternion rotation = {};
    Vector3 scale{1.0f, 1.0f, 1.0f};

    static Pose Of(const Transform& transform);
    void ApplyTo(Transform& transform) const;
    friend bool operator==(const Pose&, const Pose&) = default;
};

// Sets entities' transforms, such as after a gizmo moved them; the transforms may already be at the new poses.
class SetTransformsCommand final : public SceneCommand {
public:
    struct Change {
        EntityId entity;
        Pose before;
        Pose after;
    };

    SetTransformsCommand(SceneDocument& document, std::string name, std::vector<Change> changes);

    std::string GetName() const override { return m_name; }
    bool Execute() override;
    void Undo() override;

private:
    std::string m_name;
    std::vector<Change> m_changes;
};

// The component at that place among the entity's components.
Component& GetComponentAt(Entity& entity, std::size_t index);

// Adds a component of a registered type, at a place or last: a new one, or one from text in the format of
// SaveComponentToString(), such as a pasted component.
class AddComponentCommand final : public SceneCommand {
public:
    AddComponentCommand(SceneDocument& document, EntityId entity, std::string typeName, std::string text = {},
                        std::optional<std::size_t> index = std::nullopt);

    std::string GetName() const override { return "Add " + m_typeName; }
    bool Execute() override;
    void Undo() override;

private:
    EntityId m_entity;
    std::string m_typeName;
    std::string m_text;
    std::optional<std::size_t> m_index;
};

class RemoveComponentCommand final : public SceneCommand {
public:
    RemoveComponentCommand(SceneDocument& document, EntityId entity, std::size_t index);

    std::string GetName() const override { return "Remove " + m_typeName; }
    bool Execute() override;
    void Undo() override;

private:
    EntityId m_entity;
    std::size_t m_index;
    std::string m_typeName;
    std::string m_text;
};

class MoveComponentCommand final : public SceneCommand {
public:
    MoveComponentCommand(SceneDocument& document, EntityId entity, std::size_t from, std::size_t to);

    std::string GetName() const override { return "Move Component"; }
    bool Execute() override;
    void Undo() override;

private:
    EntityId m_entity;
    std::size_t m_from;
    std::size_t m_to;
};

// Changes a component's fields from one saved state to another; the component may already be in the new one, as
// after editing it in the inspector.
class EditComponentCommand final : public SceneCommand {
public:
    EditComponentCommand(SceneDocument& document, EntityId entity, std::size_t index, std::string before,
                         std::string after);

    std::string GetName() const override { return "Edit " + m_typeName; }
    bool Execute() override;
    void Undo() override;

private:
    EntityId m_entity;
    std::size_t m_index;
    std::string m_typeName;
    std::string m_before;
    std::string m_after;
};

} // namespace Spot::Editor
