#include "scene/scene_commands.h"

#include <nlohmann/json.hpp>

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

Component* FindComponent(Scene& scene, EntityId entityId, ComponentId componentId) {
    if (const Entity* entity = scene.GetEntity(entityId)) {
        for (Component* component : entity->GetComponents<Component>()) {
            if (component->GetId() == componentId) {
                return component;
            }
        }
    }
    return nullptr;
}

// Replays only edited fields: a prefab reload may have changed the component's other fields since the edit.
void CopyChanges(nlohmann::json& current, const nlohmann::json& before, const nlohmann::json& after) {
    if (before == after) {
        return;
    }
    if (current.is_object() && before.is_object() && after.is_object()) {
        for (const auto& [key, value] : before.items()) {
            if (!after.contains(key)) {
                current.erase(key);
            } else if (current.contains(key)) {
                CopyChanges(current[key], value, after[key]);
            } else if (value != after[key]) {
                current[key] = after[key];
            }
        }
        for (const auto& [key, value] : after.items()) {
            if (!before.contains(key)) {
                current[key] = value;
            }
        }
    } else {
        current = after;
    }
}

bool ReplayComponentEdit(Component& component, std::string_view before, std::string_view after, AssetManager* assets) {
    auto current = nlohmann::json::parse(SaveComponentToString(component), nullptr, false);
    const auto from = nlohmann::json::parse(before, nullptr, false);
    const auto to = nlohmann::json::parse(after, nullptr, false);
    if (!current.is_object() || !from.is_object() || !to.is_object()) {
        return false;
    }
    CopyChanges(current, from, to);
    return LoadComponentFromString(component, current.dump(), assets);
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
        const std::vector<Entity*> roots =
            scene.Instantiate(placement.text, {.parent = parent,
                                               .siblingIndex = index,
                                               .keepIds = m_executed,
                                               .keepExternalReferences = true,
                                               .assets = GetDocument().GetAssetManager()});
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
        if (Entity* entity = GetScene().GetEntity(id)) {
            entity->Destroy();
        }
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
    m_prefabs.clear();
    for (Entity* root : roots) {
        Entity* prefab = scene.GetEntity(root->GetPrefabRootId());
        if (prefab && prefab != root) {
            if (std::ranges::none_of(m_prefabs, [&](const auto& saved) { return saved.first == prefab->GetId(); })) {
                m_prefabs.push_back({prefab->GetId(),
                                     {.parent = prefab->GetParent() ? prefab->GetParent()->GetId() : EntityId::Invalid,
                                      .index = prefab->GetSiblingIndex(),
                                      .text = prefab->SaveToString()}});
            }
        }
    }
    std::vector<Entity*> ordinary;
    for (Entity* root : roots) {
        const bool covered = std::ranges::any_of(
            m_prefabs, [&](const auto& saved) { return root->IsDescendantOf(*scene.GetEntity(saved.first)); });
        if (!covered) {
            ordinary.push_back(root);
            m_deleted.push_back({.parent = root->GetParent() ? root->GetParent()->GetId() : EntityId::Invalid,
                                 .index = root->GetSiblingIndex(),
                                 .text = {}});
        }
    }
    // Saved together, so references between them come back too.
    if (!m_deleted.empty()) {
        m_deleted.front().text = scene.SaveEntitiesToString(ordinary);
    }
    for (Entity* root : roots) {
        root->Destroy();
    }
    return true;
}

void DeleteEntitiesCommand::Undo() {
    Scene& scene = GetScene();
    nlohmann::json combined = {{"format", "spot-scene"}, {"version", 2}, {"entities", nlohmann::json::array()}};
    for (const auto& [id, saved] : m_prefabs) {
        (void)id;
        const auto document = nlohmann::json::parse(saved.text);
        for (const auto& entity : document["entities"]) {
            combined["entities"].push_back(entity);
        }
    }
    if (!m_deleted.empty()) {
        const auto document = nlohmann::json::parse(m_deleted.front().text);
        for (const auto& entity : document["entities"]) {
            combined["entities"].push_back(entity);
        }
    }
    const std::string text = combined.dump();
    Scene prepared;
    if (prepared.Instantiate(text, {.assets = GetDocument().GetAssetManager()}).empty()) {
        return;
    }
    for (const auto& [id, saved] : m_prefabs) {
        (void)saved;
        if (Entity* instance = scene.GetEntity(id)) {
            instance->Destroy();
        }
    }
    const auto roots = scene.Instantiate(
        text, {.keepIds = true, .keepExternalReferences = true, .assets = GetDocument().GetAssetManager()});
    // Back in hierarchy order, so the earlier places exist when the later ones are taken.
    for (std::size_t i = 0; i < roots.size(); ++i) {
        const Deleted& saved = i < m_prefabs.size() ? m_prefabs[i].second : m_deleted[i - m_prefabs.size()];
        roots[i]->SetParent(scene.GetEntity(saved.parent));
        roots[i]->SetSiblingIndex(saved.index);
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
        Entity* restored = scene.GetEntity(previous.entity);
        if (!restored) {
            continue;
        }
        Entity& entity = *restored;
        entity.SetParent(scene.GetEntity(previous.parent));
        entity.SetSiblingIndex(previous.index);
        Pose{previous.position, previous.rotation, previous.scale}.ApplyTo(entity.GetTransform());
    }
}

RenameEntityCommand::RenameEntityCommand(SceneDocument& document, EntityId entity, std::string name)
    : SceneCommand(document), m_entity(entity), m_name(std::move(name)) {
}

bool RenameEntityCommand::Execute() {
    Entity* found = GetScene().GetEntity(m_entity);
    if (!found) {
        return false;
    }
    Entity& entity = *found;
    if (entity.GetName() == m_name) {
        return false;
    }
    m_previous = entity.GetName();
    entity.SetName(m_name);
    return true;
}

void RenameEntityCommand::Undo() {
    if (Entity* entity = GetScene().GetEntity(m_entity)) {
        entity->SetName(m_previous);
    }
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
        if (Entity* entity = GetScene().GetEntity(id)) {
            entity->SetActive(!m_active);
        }
    }
}

SetEntitiesLayerCommand::SetEntitiesLayerCommand(SceneDocument& document, std::vector<EntityId> entities, int layer)
    : SceneCommand(document), m_entities(std::move(entities)), m_layer(layer) {
}

bool SetEntitiesLayerCommand::Execute() {
    m_previous.clear();
    for (const EntityId id : m_entities) {
        if (Entity* entity = GetScene().GetEntity(id); entity && entity->GetLayer() != m_layer) {
            m_previous.emplace_back(id, entity->GetLayer());
            entity->SetLayer(m_layer);
        }
    }
    return !m_previous.empty();
}

void SetEntitiesLayerCommand::Undo() {
    for (const auto& [id, layer] : m_previous) {
        if (Entity* entity = GetScene().GetEntity(id)) {
            entity->SetLayer(layer);
        }
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
        Entity* found = GetScene().GetEntity(previous.entity);
        if (!found) {
            continue;
        }
        Entity& entity = *found;
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
        if (Entity* entity = GetScene().GetEntity(change.entity)) {
            change.after.ApplyTo(entity->GetTransform());
            changed = changed || change.before != change.after;
        }
    }
    return changed;
}

void SetTransformsCommand::Undo() {
    for (const Change& change : m_changes) {
        if (Entity* entity = GetScene().GetEntity(change.entity)) {
            change.before.ApplyTo(entity->GetTransform());
        }
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
    Entity* found = GetScene().GetEntity(m_entity);
    if (!found) {
        return false;
    }
    Entity& entity = *found;
    Component* added = nullptr;
    if (m_text.empty()) {
        // A new component; its defaults are saved so redoing brings back the same one.
        if (std::unique_ptr<Component> component = CreateComponent(m_typeName)) {
            added = &entity.AddComponent(std::move(component), m_index);
            m_text = SaveComponentToString(*added);
        }
    } else {
        added = AddComponentFromString(entity, m_text, m_index, GetDocument().GetAssetManager());
    }
    if (!added) {
        return false;
    }
    m_index = entity.GetComponentIndex(*added);
    m_component = added->GetId();
    m_text = SaveComponentToString(*added);
    return true;
}

void AddComponentCommand::Undo() {
    if (Component* component = FindComponent(GetScene(), m_entity, m_component)) {
        GetScene().GetEntity(m_entity)->RemoveComponent(*component);
    }
}

RemoveComponentCommand::RemoveComponentCommand(SceneDocument& document, EntityId entity, std::size_t index)
    : SceneCommand(document), m_entity(entity), m_index(index),
      m_typeName(GetComponentTypeName(GetComponentAt(GetEntity(entity), index))),
      m_component(GetComponentAt(GetEntity(entity), index).GetId()) {
}

bool RemoveComponentCommand::Execute() {
    Component* component = FindComponent(GetScene(), m_entity, m_component);
    if (!component) {
        return false;
    }
    Entity& entity = *GetScene().GetEntity(m_entity);
    m_index = entity.GetComponentIndex(*component);
    m_text = SaveComponentToString(*component);
    entity.RemoveComponent(*component);
    return true;
}

void RemoveComponentCommand::Undo() {
    if (Entity* entity = GetScene().GetEntity(m_entity); entity && !FindComponent(GetScene(), m_entity, m_component)) {
        AddComponentFromString(*entity, m_text, m_index, GetDocument().GetAssetManager());
    }
}

MoveComponentCommand::MoveComponentCommand(SceneDocument& document, EntityId entity, std::size_t from, std::size_t to)
    : SceneCommand(document), m_entity(entity), m_from(from), m_to(to),
      m_component(GetComponentAt(GetEntity(entity), from).GetId()) {
}

bool MoveComponentCommand::Execute() {
    if (m_from == m_to) {
        return false;
    }
    Component* component = FindComponent(GetScene(), m_entity, m_component);
    if (!component) {
        return false;
    }
    GetScene().GetEntity(m_entity)->MoveComponent(*component, m_to);
    return true;
}

void MoveComponentCommand::Undo() {
    if (Component* component = FindComponent(GetScene(), m_entity, m_component)) {
        GetScene().GetEntity(m_entity)->MoveComponent(*component, m_from);
    }
}

EditComponentCommand::EditComponentCommand(SceneDocument& document, EntityId entity, std::size_t index,
                                           std::string before, std::string after)
    : SceneCommand(document), m_entity(entity), m_component(GetComponentAt(GetEntity(entity), index).GetId()),
      m_typeName(GetComponentTypeName(GetComponentAt(GetEntity(entity), index))), m_before(std::move(before)),
      m_after(std::move(after)) {
}

bool EditComponentCommand::Execute() {
    if (m_before == m_after) {
        return false;
    }
    Component* component = FindComponent(GetScene(), m_entity, m_component);
    return component && ReplayComponentEdit(*component, m_before, m_after, GetDocument().GetAssetManager());
}

void EditComponentCommand::Undo() {
    if (Component* component = FindComponent(GetScene(), m_entity, m_component)) {
        ReplayComponentEdit(*component, m_after, m_before, GetDocument().GetAssetManager());
    }
}

ReplaceSceneCommand::ReplaceSceneCommand(SceneDocument& document, std::string text, std::string name)
    : SceneCommand(document), m_text(std::move(text)), m_name(std::move(name)) {
}

bool ReplaceSceneCommand::Execute() {
    Scene& scene = GetScene();
    if (m_before.empty()) {
        m_before = scene.SaveToString();
    }
    return GetDocument().ReplaceSceneText(m_text);
}

void ReplaceSceneCommand::Undo() {
    GetDocument().ReplaceSceneText(m_before);
}

} // namespace Spot::Editor
