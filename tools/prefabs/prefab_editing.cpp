#include "prefabs/prefab_editing.h"

#include <spot/core/debug/log.h>
#include <spot/scene/prefab.h>
#include <spot/scene/prefab_document.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace Spot {

namespace {

using nlohmann::json;
using IdSet = std::unordered_set<std::uint64_t>;

json MakeDocument(json entities = json::array()) {
    return {{"format", "spot-scene"}, {"version", 2}, {"entities", std::move(entities)}};
}

std::string Dump(const json& document) {
    return document.dump(2, ' ', false, json::error_handler_t::replace);
}

std::uint64_t GetId(const json& node, std::string_view key) {
    const auto found = node.find(key);
    return found != node.end() && found->is_number_unsigned() ? found->get<std::uint64_t>() : 0;
}

std::string Escape(std::string_view key) {
    std::string result;
    for (const char c : key) {
        result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    }
    return result;
}

bool IsMarker(const json& value) {
    return value.is_object() && value.size() == 1 && (value.contains("prefabEntity") || value.contains("sceneEntity"));
}

// Arrays and entity references are values, so inserting one item does not override unrelated indices after the
// source changes, and a reference replaces the plain id prefabs keep.
void Diff(const json& before, const json& after, std::string_view path, json& patches) {
    if (before == after) {
        return;
    }
    if (!before.is_object() || !after.is_object() || IsMarker(before) || IsMarker(after)) {
        patches.push_back({{"op", "replace"}, {"path", path}, {"value", after}});
        return;
    }
    for (const auto& [key, value] : before.items()) {
        const std::string child = std::string(path) + '/' + Escape(key);
        if (!after.contains(key)) {
            patches.push_back({{"op", "remove"}, {"path", child}});
        } else {
            Diff(value, after[key], child, patches);
        }
    }
    for (const auto& [key, value] : after.items()) {
        if (!before.contains(key)) {
            patches.push_back({{"op", "add"}, {"path", std::string(path) + '/' + Escape(key)}, {"value", value}});
        }
    }
}

// The place of each saved entity among the saved children of its parent, which is its sibling index when they are
// all saved, as they are below an instance's root.
std::vector<std::size_t> GetSiblingIndices(const json& entities) {
    std::unordered_map<std::uint64_t, std::size_t> counts;
    std::vector<std::size_t> indices;
    for (const json& node : entities) {
        indices.push_back(counts[GetId(node, "parent")]++);
    }
    return indices;
}

struct KeyedInstance {
    // The keyed form of each entity of the instance, by its id in the prefab.
    json entities = json::object();
    // The saved id of each, by its id in the prefab.
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
};

KeyedInstance KeyInstance(const json& entities, const std::vector<std::size_t>& siblings, std::uint64_t root) {
    KeyedInstance instance;
    std::unordered_map<std::uint64_t, std::uint64_t> locals;
    for (const json& node : entities) {
        if (GetId(node, "prefabRoot") == root && GetId(node, "prefabLocal") != 0) {
            locals[GetId(node, "id")] = GetId(node, "prefabLocal");
        }
    }
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const json& node = entities[i];
        const auto found = locals.find(GetId(node, "id"));
        if (found == locals.end()) {
            continue;
        }
        json keyed = Detail::KeyPrefabEntity(node);
        if (found->first != root) {
            const auto parent = GetId(node, "parent");
            const auto inside = locals.find(parent);
            keyed["parent"] =
                inside != locals.end() ? json{{"prefabEntity", inside->second}} : json{{"sceneEntity", parent}};
            keyed["siblingIndex"] = siblings[i];
        }
        instance.entities[std::to_string(found->second)] = std::move(keyed);
        instance.ids[found->second] = found->first;
    }
    return instance;
}

// Instantiating a prefab to see what its instances start from is kept for as long as the prefab is.
struct Baseline {
    AssetRef<Prefab> prefab;
    std::shared_ptr<const json> entities;
};
std::mutex s_baselineMutex;
std::list<Baseline> s_baselines;

std::shared_ptr<const json> GetBaseline(const AssetRef<Prefab>& prefab, AssetManager* assets) {
    const std::scoped_lock lock(s_baselineMutex);
    std::erase_if(s_baselines, [&](const Baseline& baseline) {
        return baseline.prefab.GetShared().use_count() == 1 && baseline.prefab.Get() != prefab.Get();
    });
    for (const Baseline& baseline : s_baselines) {
        if (baseline.prefab.Get() == prefab.Get()) {
            return baseline.entities;
        }
    }
    Scene scratch;
    json entities = json::object();
    if (Entity* root = scratch.Instantiate(prefab, {.assets = assets, .keepUnknownComponents = true})) {
        const json saved = json::parse(root->SaveToString());
        entities = KeyInstance(saved["entities"], GetSiblingIndices(saved["entities"]),
                               static_cast<std::uint64_t>(root->GetId()))
                       .entities;
    }
    auto shared = std::make_shared<const json>(std::move(entities));
    s_baselines.push_back({prefab, shared});
    return shared;
}

// Turns the linked prefab instances of a saved document into records of their overrides.
json Compact(json document, const Scene& scene, AssetManager* assets) {
    json& entities = document["entities"];
    const auto siblings = GetSiblingIndices(entities);
    std::unordered_map<std::uint64_t, json> records;
    IdSet members;
    for (const json& node : entities) {
        const auto id = GetId(node, "id");
        const Entity* root = scene.GetEntity(static_cast<EntityId>(id));
        if (!node.contains("prefabAsset") || GetId(node, "prefabRoot") != id || !root || !root->GetPrefabAsset()) {
            continue;
        }
        const auto baseline = GetBaseline(root->GetPrefabAsset(), assets);
        const KeyedInstance instance = KeyInstance(entities, siblings, id);
        json record{{"id", id},
                    {"prefab", node["prefabAsset"]},
                    {"entityIds", json::object()},
                    {"overrides", json::object()},
                    {"removedEntities", json::array()}};
        if (node.contains("parent")) {
            record["parent"] = node["parent"];
        }
        for (const auto& [key, current] : instance.entities.items()) {
            const auto local = *Detail::ParsePrefabLocalId(key);
            record["entityIds"][key] = instance.ids.at(local);
            members.insert(instance.ids.at(local));
            if (baseline->contains(key)) {
                json patches = json::array();
                Diff((*baseline)[key], current, "", patches);
                if (!patches.empty()) {
                    record["overrides"][key] = std::move(patches);
                }
            }
        }
        std::vector<std::uint64_t> removed;
        for (const auto& [key, unused] : baseline->items()) {
            (void)unused;
            if (!instance.entities.contains(key)) {
                removed.push_back(*Detail::ParsePrefabLocalId(key));
            }
        }
        std::ranges::sort(removed);
        record["removedEntities"] = removed;
        records[id] = std::move(record);
    }
    json compacted = json::array();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        json& node = entities[i];
        const auto id = GetId(node, "id");
        const auto instance = records.find(GetId(node, "prefabRoot"));
        json saved;
        if (instance != records.end()) {
            if (id != instance->first) {
                continue;
            }
            saved = std::move(instance->second);
        } else {
            saved = std::move(node);
        }
        // Entities added below an instance's entities keep their place among them.
        if (members.contains(GetId(saved, "parent"))) {
            saved["siblingIndex"] = siblings[i];
        }
        compacted.push_back(std::move(saved));
    }
    entities = std::move(compacted);
    return document;
}

bool IsRoot(const Entity& root) {
    return root.GetPrefabRootId() == root.GetId() && root.GetPrefabAsset() &&
           root.GetScene().GetEntity(root.GetId()) == &root;
}

json SaveInstance(const Entity& root, AssetManager* assets) {
    Entity* entities[] = {const_cast<Entity*>(&root)};
    return Compact(json::parse(root.GetScene().SaveEntitiesToString(entities)), root.GetScene(), assets);
}

json* FindRecord(json& document, std::uint64_t id) {
    for (json& node : document["entities"]) {
        if (GetId(node, "id") == id && node.contains("prefab")) {
            return &node;
        }
    }
    return nullptr;
}

IdSet GetMembers(const json& record) {
    IdSet members;
    for (const json& id : record["entityIds"]) {
        members.insert(id.get<std::uint64_t>());
    }
    return members;
}

bool Selects(const PrefabOverrideScope& scope, std::uint64_t entity, std::string_view path) {
    if (scope.entity != 0 && scope.entity != entity) {
        return false;
    }
    const auto contains = [](std::string_view parent, std::string_view child) {
        return parent.empty() ||
               (child.starts_with(parent) && (child.size() == parent.size() || child[parent.size()] == '/'));
    };
    return contains(scope.pointer, path) || contains(path, scope.pointer);
}

std::string AdditionPath(std::uint64_t id) {
    return "/addedEntities/" + std::to_string(id);
}

// Makes the instance's entities ordinary, their references to each other plain ids.
void Unlink(json& entities, std::uint64_t root) {
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    for (const json& node : entities) {
        if (GetId(node, "prefabRoot") == root) {
            ids[GetId(node, "prefabLocal")] = GetId(node, "id");
        }
    }
    for (json& node : entities) {
        if (GetId(node, "prefabRoot") != root) {
            continue;
        }
        node.erase("prefabRoot");
        node.erase("prefabLocal");
        node.erase("prefabAsset");
        Detail::ForEachEntityMarker(node["components"], [&](json& marker) {
            if (marker.contains("prefabEntity")) {
                const auto found = ids.find(GetId(marker, "prefabEntity"));
                marker = found != ids.end() ? found->second : 0;
            } else {
                marker = GetId(marker, "sceneEntity");
            }
        });
    }
}

// Links the entities of the prefabs nested in a prefab's source again, after the source was made from an instance's
// ordinary entities, which keep the ids they had in it.
std::optional<std::string> Relink(json document, std::string_view source, AssetManager* assets) {
    const auto original = source.empty() ? std::nullopt : Prefab::FromString(source);
    if (original && !original->GetDependencies().empty()) {
        Scene before;
        if (before.Instantiate(source, {.keepIds = true, .assets = assets, .keepUnknownComponents = true}).empty()) {
            return std::nullopt;
        }
        const json linked = json::parse(before.SaveToString());
        std::unordered_map<std::uint64_t, const json*> links;
        for (const json& node : linked["entities"]) {
            if (node.contains("prefabRoot")) {
                links[GetId(node, "id")] = &node;
            }
        }
        IdSet ids;
        for (const json& node : document["entities"]) {
            ids.insert(GetId(node, "id"));
        }
        for (json& node : document["entities"]) {
            const auto link = links.find(GetId(node, "id"));
            if (node.contains("prefabRoot") || link == links.end() ||
                !ids.contains(GetId(*link->second, "prefabRoot"))) {
                continue;
            }
            for (const char* field : {"prefabRoot", "prefabLocal", "prefabAsset"}) {
                if (link->second->contains(field)) {
                    node[field] = (*link->second)[field];
                }
            }
        }
    }
    Scene after;
    const auto roots =
        after.Instantiate(Dump(document), {.keepIds = true, .assets = assets, .keepUnknownComponents = true});
    if (roots.size() != 1) {
        return std::nullopt;
    }
    return SaveEntitySource(*roots.front(), assets);
}

} // namespace

std::string SaveSceneSource(const Scene& scene, AssetManager* assets) {
    return Dump(Compact(json::parse(scene.SaveToString()), scene, assets));
}

std::string SaveEntitySource(const Entity& entity, AssetManager* assets) {
    return Dump(Compact(json::parse(entity.SaveToString()), entity.GetScene(), assets));
}

std::vector<PrefabOverride> GetPrefabOverrides(const Entity& root, AssetManager* assets) {
    if (!IsRoot(root)) {
        return {};
    }
    json document = SaveInstance(root, assets);
    const json* record = FindRecord(document, static_cast<std::uint64_t>(root.GetId()));
    if (!record) {
        return {};
    }
    std::vector<PrefabOverride> result;
    for (const auto& [local, patches] : (*record)["overrides"].items()) {
        for (const json& patch : patches) {
            result.push_back({{*Detail::ParsePrefabLocalId(local), patch["path"].get<std::string>()},
                              patch["op"].get<std::string>()});
        }
    }
    for (const json& local : (*record)["removedEntities"]) {
        result.push_back({{local.get<std::uint64_t>(), {}}, "remove entity"});
    }
    const IdSet members = GetMembers(*record);
    for (const json& node : document["entities"]) {
        if (&node != record && members.contains(GetId(node, "parent"))) {
            result.push_back({{0, AdditionPath(GetId(node, "id"))}, "add entity"});
        }
    }
    return result;
}

std::optional<PrefabEdit> PreparePrefabEdit(const Entity& root, const PrefabOverrideScope& scope, bool apply,
                                            std::string_view prefabSource, AssetManager* assets) {
    if (!IsRoot(root)) {
        return std::nullopt;
    }
    const auto changes = GetPrefabOverrides(root, assets);
    if (std::ranges::none_of(changes, [&](const PrefabOverride& change) {
            return Selects(scope, change.scope.entity, change.scope.pointer);
        })) {
        return std::nullopt;
    }
    const Scene& scene = root.GetScene();
    json document = Compact(json::parse(scene.SaveToString()), scene, assets);
    json* found = FindRecord(document, static_cast<std::uint64_t>(root.GetId()));
    if (!found) {
        return std::nullopt;
    }
    json record = *found;
    json selected = json::object();
    for (auto& [key, patches] : record["overrides"].items()) {
        const auto local = *Detail::ParsePrefabLocalId(key);
        std::erase_if(patches.get_ref<json::array_t&>(), [&](const json& patch) {
            if (!Selects(scope, local, patch["path"].get<std::string>())) {
                return false;
            }
            selected[key].push_back(patch);
            return true;
        });
    }
    std::erase_if(record["overrides"].get_ref<json::object_t&>(),
                  [](const auto& entry) { return entry.second.empty(); });
    json removed = json::array();
    std::erase_if(record["removedEntities"].get_ref<json::array_t&>(), [&](const json& local) {
        if (!scope.pointer.empty() || !Selects(scope, local.get<std::uint64_t>(), {})) {
            return false;
        }
        removed.push_back(local);
        return true;
    });
    // Selected additions go with their descendants, which come after them.
    const IdSet members = GetMembers(record);
    IdSet additions;
    json added = json::array();
    for (const json& node : document["entities"]) {
        const auto id = GetId(node, "id");
        const auto parent = GetId(node, "parent");
        if (&node != found &&
            ((members.contains(parent) && Selects(scope, 0, AdditionPath(id))) || additions.contains(parent))) {
            additions.insert(id);
            added.push_back(node);
        }
    }
    *found = record;
    std::erase_if(document["entities"].get_ref<json::array_t&>(),
                  [&](const json& node) { return additions.contains(GetId(node, "id")); });
    PrefabEdit edit{.sceneText = Dump(document), .prefabText = {}};
    if (!apply) {
        return edit;
    }

    // The instance with only the selected changes, its entities renumbered with their ids in the prefab and the
    // additions given new ones, is the new prefab.
    json applied = record;
    applied["overrides"] = std::move(selected);
    applied["removedEntities"] = std::move(removed);
    applied.erase("parent");
    applied.erase("siblingIndex");
    std::unordered_map<EntityId, EntityId> ids;
    std::uint64_t next = 1;
    for (const auto& [key, id] : record["entityIds"].items()) {
        const auto local = *Detail::ParsePrefabLocalId(key);
        ids[static_cast<EntityId>(id.get<std::uint64_t>())] = static_cast<EntityId>(local);
        next = std::max(next, local + 1);
    }
    for (const auto& [key, unused] : GetBaseline(root.GetPrefabAsset(), assets)->items()) {
        (void)unused;
        next = std::max(next, *Detail::ParsePrefabLocalId(key) + 1);
    }
    json entities = json::array({std::move(applied)});
    entities.insert(entities.end(), added.begin(), added.end());
    const AssetRef<Prefab>& prefab = root.GetPrefabAsset();
    const auto combined = Prefab::FromString(Dump(MakeDocument(std::move(entities))));
    const auto flat = combined ? combined->Flatten([&](AssetId id) {
        return id == prefab.GetId() ? prefab : assets ? assets->Load<Prefab>(id) : AssetRef<Prefab>();
    })
                               : std::nullopt;
    if (!flat) {
        return std::nullopt;
    }
    for (const json& node : flat->GetDocument().document["entities"]) {
        if (const auto id = static_cast<EntityId>(GetId(node, "id")); !ids.contains(id)) {
            ids[id] = static_cast<EntityId>(next++);
        }
    }
    Scene scratch;
    const auto roots =
        scratch.Instantiate(flat->GetEntityText(), {.assets = assets, .ids = &ids, .keepUnknownComponents = true});
    if (roots.size() != 1) {
        return std::nullopt;
    }
    // The additions become the prefab's, keeping their ids in the scene.
    json* current = FindRecord(document, static_cast<std::uint64_t>(root.GetId()));
    for (const auto& [id, local] : ids) {
        if (!members.contains(static_cast<std::uint64_t>(id))) {
            (*current)["entityIds"][std::to_string(static_cast<std::uint64_t>(local))] = static_cast<std::uint64_t>(id);
        }
    }
    edit.sceneText = Dump(document);
    const auto text = Relink(json::parse(scratch.SaveToString()), prefabSource, assets);
    const auto result = text ? Prefab::FromString(*text) : std::nullopt;
    if (!result) {
        return std::nullopt;
    }
    edit.prefabText = result->GetEntityText();
    return edit;
}

std::optional<std::string> UnpackPrefabToString(const Entity& root) {
    if (!IsRoot(root)) {
        return std::nullopt;
    }
    json document = json::parse(root.GetScene().SaveToString());
    Unlink(document["entities"], static_cast<std::uint64_t>(root.GetId()));
    return Dump(document);
}

} // namespace Spot
