#include "scene/scene_commands.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Spot::Editor {

namespace {

// The sibling indices from the root down to the entity, which order entities as a hierarchy shows them.
std::vector<std::size_t> GetHierarchyPath(const Entity& entity) {
    std::vector<std::size_t> path;
    for (const Entity* node = &entity; node; node = node->GetParent()) {
        path.push_back(node->GetSiblingIndex());
    }
    std::ranges::reverse(path);
    return path;
}

// The entities that exist, leaving out those below others of the list, which go along with them, in hierarchy order.
std::vector<Entity*> GetTopmost(Scene& scene, const std::vector<EntityId>& ids) {
    std::vector<Entity*> entities;
    for (const EntityId id : ids) {
        if (Entity* entity = scene.GetEntity(id); entity && std::ranges::find(entities, entity) == entities.end()) {
            entities.push_back(entity);
        }
    }
    std::erase_if(entities, [&](const Entity* entity) {
        return std::ranges::any_of(
            entities, [&](const Entity* other) { return other != entity && entity->IsDescendantOf(*other); });
    });
    std::ranges::sort(entities, {}, [](const Entity* entity) { return GetHierarchyPath(*entity); });
    return entities;
}

std::vector<EntityId> GetIds(const std::vector<Entity*>& entities) {
    std::vector<EntityId> ids;
    ids.reserve(entities.size());
    for (const Entity* entity : entities) {
        ids.push_back(entity->GetId());
    }
    return ids;
}

} // namespace

SceneCommandGroup::SceneCommandGroup(SceneDocument& document, std::string name)
    : SceneCommand(document), m_name(std::move(name)) {
}

bool SceneCommandGroup::Execute() {
    if (m_executed) {
        for (const auto& command : m_commands) {
            command->Execute();
        }
        return true;
    }
    std::erase_if(m_commands, [](const auto& command) { return !command->Execute(); });
    m_executed = true;
    return !m_commands.empty();
}

void SceneCommandGroup::Undo() {
    for (auto command = m_commands.rbegin(); command != m_commands.rend(); ++command) {
        (*command)->Undo();
    }
}

std::optional<std::vector<EntityId>> SceneCommandGroup::GetSelectionAfter() const {
    for (auto command = m_commands.rbegin(); command != m_commands.rend(); ++command) {
        if (auto selection = (*command)->GetSelectionAfter()) {
            return selection;
        }
    }
    return std::nullopt;
}

CreateEntitiesCommand::CreateEntitiesCommand(SceneDocument& document, std::string name, std::string text,
                                             EntityId parent, EntityId before)
    : CreateEntitiesCommand(document, std::move(name), std::vector<Placement>{{std::move(text), parent, before}}) {
}

CreateEntitiesCommand::CreateEntitiesCommand(SceneDocument& document, std::string name,
                                             std::vector<Placement> placements)
    : SceneCommand(document), m_name(std::move(name)), m_placements(std::move(placements)) {
}

bool CreateEntitiesCommand::Execute() {
    Scene& scene = GetScene();
    m_created.clear();
    for (Placement& placement : m_placements) {
        Entity* parent = scene.GetEntity(placement.parent);
        if (placement.parent != EntityId::Invalid && !parent) {
            continue;
        }
        std::optional<std::size_t> index;
        if (const Entity* before = scene.GetEntity(placement.before); before && before->GetParent() == parent) {
            index = before->GetSiblingIndex();
        }
        // The first time they get new ids, as copies of entities still in the scene must; afterwards the same ones.
        const std::vector<Entity*> roots = scene.Instantiate(
            placement.text,
            {.parent = parent, .siblingIndex = index, .keepIds = m_executed, .keepExternalReferences = true});
        if (!m_executed) {
            placement.text = scene.SaveEntitiesToString(roots);
        }
        const std::vector<EntityId> ids = GetIds(roots);
        m_created.insert(m_created.end(), ids.begin(), ids.end());
    }
    m_executed = true;
    return !m_created.empty();
}

void CreateEntitiesCommand::Undo() {
    for (const EntityId id : m_created) {
        GetEntity(id).Destroy();
    }
}

DeleteEntitiesCommand::DeleteEntitiesCommand(SceneDocument& document, std::vector<EntityId> entities)
    : SceneCommand(document), m_entities(std::move(entities)) {
}

std::string DeleteEntitiesCommand::GetName() const {
    return m_entities.size() == 1 ? "Delete Entity" : "Delete Entities";
}

bool DeleteEntitiesCommand::Execute() {
    Scene& scene = GetScene();
    const std::vector<Entity*> roots = GetTopmost(scene, m_entities);
    if (roots.empty()) {
        return false;
    }
    m_deleted.clear();
    for (const Entity* root : roots) {
        m_deleted.push_back({.parent = root->GetParent() ? root->GetParent()->GetId() : EntityId::Invalid,
                             .index = root->GetSiblingIndex(),
                             .text = {}});
    }
    // Saved together, so references between them come back too.
    m_deleted.front().text = scene.SaveEntitiesToString(roots);
    for (Entity* root : roots) {
        root->Destroy();
    }
    return true;
}

void DeleteEntitiesCommand::Undo() {
    Scene& scene = GetScene();
    const std::vector<Entity*> roots =
        scene.Instantiate(m_deleted.front().text, {.keepIds = true, .keepExternalReferences = true});
    // Back in hierarchy order, so the earlier places exist when the later ones are taken.
    for (std::size_t i = 0; i < roots.size() && i < m_deleted.size(); ++i) {
        roots[i]->SetParent(scene.GetEntity(m_deleted[i].parent));
        roots[i]->SetSiblingIndex(m_deleted[i].index);
    }
}

ReparentEntitiesCommand::ReparentEntitiesCommand(SceneDocument& document, std::vector<EntityId> entities,
                                                 EntityId parent, EntityId before)
    : SceneCommand(document), m_entities(std::move(entities)), m_parent(parent), m_before(before) {
}

bool ReparentEntitiesCommand::Execute() {
    Scene& scene = GetScene();
    Entity* parent = scene.GetEntity(m_parent);
    if (m_parent != EntityId::Invalid && !parent) {
        return false;
    }
    std::vector<Entity*> moved = GetTopmost(scene, m_entities);
    // An entity cannot go below itself.
    std::erase_if(
        moved, [&](const Entity* entity) { return parent && (parent == entity || parent->IsDescendantOf(*entity)); });

    m_previous.clear();
    for (const Entity* entity : moved) {
        const Pose pose = Pose::Of(entity->GetTransform());
        m_previous.push_back({.entity = entity->GetId(),
                              .parent = entity->GetParent() ? entity->GetParent()->GetId() : EntityId::Invalid,
                              .index = entity->GetSiblingIndex(),
                              .position = pose.position,
                              .rotation = pose.rotation,
                              .scale = pose.scale});
    }

    const Entity* before = scene.GetEntity(m_before);
    bool changed = false;
    for (Entity* entity : moved) {
        const Entity* oldParent = entity->GetParent();
        const std::size_t oldIndex = entity->GetSiblingIndex();
        entity->SetParent(parent, true);
        // Without an entity to go before, they go last, also when the parent stays the same.
        const bool placed = before && before != entity && before->GetParent() == parent;
        entity->SetSiblingIndex(placed ? before->GetSiblingIndex() : std::numeric_limits<std::size_t>::max());
        changed = changed || oldParent != entity->GetParent() || oldIndex != entity->GetSiblingIndex();
    }
    return changed;
}

void ReparentEntitiesCommand::Undo() {
    Scene& scene = GetScene();
    for (const Previous& previous : m_previous) {
        Entity& entity = GetEntity(previous.entity);
        entity.SetParent(scene.GetEntity(previous.parent));
        entity.SetSiblingIndex(previous.index);
        Pose{previous.position, previous.rotation, previous.scale}.ApplyTo(entity.GetTransform());
    }
}

RenameEntityCommand::RenameEntityCommand(SceneDocument& document, EntityId entity, std::string name)
    : SceneCommand(document), m_entity(entity), m_name(std::move(name)) {
}

bool RenameEntityCommand::Execute() {
    Entity& entity = GetEntity(m_entity);
    if (entity.GetName() == m_name) {
        return false;
    }
    m_previous = entity.GetName();
    entity.SetName(m_name);
    return true;
}

void RenameEntityCommand::Undo() {
    GetEntity(m_entity).SetName(m_previous);
}

SetEntitiesActiveCommand::SetEntitiesActiveCommand(SceneDocument& document, std::vector<EntityId> entities, bool active)
    : SceneCommand(document), m_entities(std::move(entities)), m_active(active) {
}

bool SetEntitiesActiveCommand::Execute() {
    m_changed.clear();
    for (const EntityId id : m_entities) {
        if (Entity* entity = GetScene().GetEntity(id); entity && entity->IsActiveSelf() != m_active) {
            entity->SetActive(m_active);
            m_changed.push_back(id);
        }
    }
    return !m_changed.empty();
}

void SetEntitiesActiveCommand::Undo() {
    for (const EntityId id : m_changed) {
        GetEntity(id).SetActive(!m_active);
    }
}

SetEntitiesTagCommand::SetEntitiesTagCommand(SceneDocument& document, std::vector<EntityId> entities, std::string tag,
                                             bool add)
    : SceneCommand(document), m_entities(std::move(entities)), m_tag(std::move(tag)), m_add(add) {
}

bool SetEntitiesTagCommand::Execute() {
    m_previous.clear();
    for (const EntityId id : m_entities) {
        Entity* entity = GetScene().GetEntity(id);
        if (!entity || entity->HasTag(m_tag) == m_add) {
            continue;
        }
        m_previous.push_back({id, {entity->GetTags().begin(), entity->GetTags().end()}});
        if (m_add) {
            entity->AddTag(m_tag);
        } else {
            entity->RemoveTag(m_tag);
        }
    }
    return !m_previous.empty();
}

void SetEntitiesTagCommand::Undo() {
    // Brings back the tags in their order, so the scene saves as it did.
    for (const Previous& previous : m_previous) {
        Entity& entity = GetEntity(previous.entity);
        const std::vector<std::string> current(entity.GetTags().begin(), entity.GetTags().end());
        for (const std::string& tag : current) {
            entity.RemoveTag(tag);
        }
        for (const std::string& tag : previous.tags) {
            entity.AddTag(tag);
        }
    }
}

Pose Pose::Of(const Transform& transform) {
    return {transform.GetPosition(), transform.GetRotation(), transform.GetScale()};
}

void Pose::ApplyTo(Transform& transform) const {
    transform.SetPosition(position);
    transform.SetRotation(rotation);
    transform.SetScale(scale);
}

SetTransformsCommand::SetTransformsCommand(SceneDocument& document, std::string name, std::vector<Change> changes)
    : SceneCommand(document), m_name(std::move(name)), m_changes(std::move(changes)) {
}

bool SetTransformsCommand::Execute() {
    bool changed = false;
    for (const Change& change : m_changes) {
        change.after.ApplyTo(GetEntity(change.entity).GetTransform());
        changed = changed || change.before != change.after;
    }
    return changed;
}

void SetTransformsCommand::Undo() {
    for (const Change& change : m_changes) {
        change.before.ApplyTo(GetEntity(change.entity).GetTransform());
    }
}

Component& GetComponentAt(Entity& entity, std::size_t index) {
    const std::vector<Component*> components = entity.GetComponents<Component>();
    SPOT_ASSERT(index < components.size(), "The entity has no component at {}", index);
    return *components[index];
}

AddComponentCommand::AddComponentCommand(SceneDocument& document, EntityId entity, std::string typeName,
                                         std::string text, std::optional<std::size_t> index)
    : SceneCommand(document), m_entity(entity), m_typeName(std::move(typeName)), m_text(std::move(text)),
      m_index(index) {
}

bool AddComponentCommand::Execute() {
    Entity& entity = GetEntity(m_entity);
    Component* added = nullptr;
    if (m_text.empty()) {
        // A new component; its defaults are saved so redoing brings back the same one.
        if (std::unique_ptr<Component> component = CreateComponent(m_typeName)) {
            added = &entity.AddComponent(std::move(component), m_index);
            m_text = SaveComponentToString(*added);
        }
    } else {
        added = AddComponentFromString(entity, m_text, m_index);
    }
    if (!added) {
        return false;
    }
    m_index = entity.GetComponentIndex(*added);
    return true;
}

void AddComponentCommand::Undo() {
    Entity& entity = GetEntity(m_entity);
    entity.RemoveComponent(GetComponentAt(entity, *m_index));
}

RemoveComponentCommand::RemoveComponentCommand(SceneDocument& document, EntityId entity, std::size_t index)
    : SceneCommand(document), m_entity(entity), m_index(index),
      m_typeName(GetComponentTypeName(GetComponentAt(GetEntity(entity), index))) {
}

bool RemoveComponentCommand::Execute() {
    Entity& entity = GetEntity(m_entity);
    Component& component = GetComponentAt(entity, m_index);
    m_text = SaveComponentToString(component);
    entity.RemoveComponent(component);
    return true;
}

void RemoveComponentCommand::Undo() {
    AddComponentFromString(GetEntity(m_entity), m_text, m_index);
}

MoveComponentCommand::MoveComponentCommand(SceneDocument& document, EntityId entity, std::size_t from, std::size_t to)
    : SceneCommand(document), m_entity(entity), m_from(from), m_to(to) {
}

bool MoveComponentCommand::Execute() {
    if (m_from == m_to) {
        return false;
    }
    Entity& entity = GetEntity(m_entity);
    entity.MoveComponent(GetComponentAt(entity, m_from), m_to);
    return true;
}

void MoveComponentCommand::Undo() {
    Entity& entity = GetEntity(m_entity);
    entity.MoveComponent(GetComponentAt(entity, m_to), m_from);
}

EditComponentCommand::EditComponentCommand(SceneDocument& document, EntityId entity, std::size_t index,
                                           std::string before, std::string after)
    : SceneCommand(document), m_entity(entity), m_index(index),
      m_typeName(GetComponentTypeName(GetComponentAt(GetEntity(entity), index))), m_before(std::move(before)),
      m_after(std::move(after)) {
}

bool EditComponentCommand::Execute() {
    if (m_before == m_after) {
        return false;
    }
    return LoadComponentFromString(GetComponentAt(GetEntity(m_entity), m_index), m_after);
}

void EditComponentCommand::Undo() {
    LoadComponentFromString(GetComponentAt(GetEntity(m_entity), m_index), m_before);
}

} // namespace Spot::Editor
