#include "spot/scene/prefab.h"

#include "spot/core/debug/log.h"
#include "spot/core/ids/asset_id.h"
#include "spot/scene/prefab_document.h"
#include "spot/scene/scene_serializer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Spot {

namespace {

using nlohmann::json;

std::optional<std::uint64_t> ReadId(const json& value) {
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
        return static_cast<std::uint64_t>(value.get<std::int64_t>());
    }
    return std::nullopt;
}

bool ValidateTree(json& entities) {
    if (!entities.is_array() || entities.empty()) {
        return false;
    }
    std::unordered_map<std::uint64_t, std::size_t> indices;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const json& entity = entities[i];
        if (!entity.is_object() || !entity.contains("id")) {
            return false;
        }
        const auto id = ReadId(entity["id"]);
        if (!id || *id == 0 || !indices.try_emplace(*id, i).second) {
            return false;
        }
    }

    std::unordered_map<std::uint64_t, std::uint64_t> inheritedParents;
    for (const json& entity : entities) {
        if (!entity.contains("prefab")) {
            continue;
        }
        const auto asset =
            entity["prefab"].is_string() ? AssetId::Parse(entity["prefab"].get<std::string>()) : std::nullopt;
        if (!asset || !asset->IsValid() || !entity.contains("entityIds") || !entity["entityIds"].is_object() ||
            !entity.contains("overrides") || !entity["overrides"].is_object() || !entity.contains("removedEntities") ||
            !entity["removedEntities"].is_array()) {
            return false;
        }
        const auto rootId = *ReadId(entity["id"]);
        std::unordered_set<std::uint64_t> mapped;
        std::unordered_set<std::uint64_t> locals;
        for (const auto& [key, value] : entity["entityIds"].items()) {
            std::uint64_t local = 0;
            const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), local);
            const auto id = ReadId(value);
            if (error != std::errc() || end != key.data() + key.size() || local == 0 || !id || *id == 0 ||
                !mapped.insert(*id).second || !locals.insert(local).second) {
                return false;
            }
            if (*id != rootId) {
                if (!indices.try_emplace(*id, indices.size()).second) {
                    return false;
                }
                inheritedParents[*id] = rootId;
            }
        }
    }

    std::optional<std::size_t> root;
    std::vector<std::vector<std::size_t>> children(indices.size());
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto savedParent = entities[i].find("parent");
        const auto parent = savedParent == entities[i].end() ? std::optional<std::uint64_t>(0) : ReadId(*savedParent);
        if (!parent) {
            return false;
        }
        const auto found = indices.find(*parent);
        if (found != indices.end()) {
            children[found->second].push_back(i);
        } else if (root) {
            return false;
        } else {
            root = i;
        }
    }
    for (const auto& [id, parent] : inheritedParents) {
        children[indices.at(parent)].push_back(indices.at(id));
    }
    if (!root) {
        return false;
    }
    // Walking from the only root must reach every node; disconnected cycles cannot be instantiated as a tree.
    std::vector<std::size_t> pending{*root};
    std::size_t visited = 0;
    while (!pending.empty()) {
        const std::size_t index = pending.back();
        pending.pop_back();
        ++visited;
        pending.insert(pending.end(), children[index].begin(), children[index].end());
    }
    if (visited != indices.size()) {
        return false;
    }
    entities[*root].erase("parent");
    for (json& entity : entities) {
        const auto components = entity.find("components");
        if (components == entity.end() || !components->is_array()) {
            continue;
        }
        std::unordered_set<AssetId> ids;
        for (std::size_t i = 0; i < components->size(); ++i) {
            json& component = (*components)[i];
            if (!component.is_object()) {
                continue;
            }
            if (!component.contains("id")) {
                // Legacy prefabs have no component ids. Deriving them from the source-local entity and slot keeps
                // their identity stable until the source is saved with persistent ids.
                component["id"] = AssetId{entity["id"].get<std::uint64_t>(), i + 1}.ToString();
            }
            const auto id =
                component["id"].is_string() ? AssetId::Parse(component["id"].get<std::string>()) : std::nullopt;
            if (!id || !id->IsValid() || !ids.insert(*id).second) {
                return false;
            }
        }
    }
    return true;
}

// Puts the entities in the order their parents keep them, as loading them would, so their places in the document
// are their sibling indices.
void NormalizeOrder(json& entities) {
    std::unordered_map<std::uint64_t, std::size_t> indices;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        indices[*ReadId(entities[i]["id"])] = i;
    }
    std::size_t root = 0;
    std::vector<std::vector<std::size_t>> children(entities.size());
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto parent = entities[i].find("parent");
        const auto found = parent != entities[i].end() && parent->is_number_unsigned()
                               ? indices.find(parent->get<std::uint64_t>())
                               : indices.end();
        if (found == indices.end()) {
            root = i;
        } else {
            children[found->second].push_back(i);
        }
    }
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto sibling = entities[i].find("siblingIndex");
        const auto parent = entities[i].find("parent");
        if (sibling == entities[i].end() || !sibling->is_number_unsigned() || parent == entities[i].end() ||
            !parent->is_number_unsigned()) {
            continue;
        }
        auto& siblings = children[indices.at(parent->get<std::uint64_t>())];
        std::erase(siblings, i);
        const auto place = std::min(sibling->get<std::size_t>(), siblings.size());
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(place), i);
    }
    json ordered = json::array();
    std::vector<std::size_t> pending{root};
    while (!pending.empty()) {
        const std::size_t index = pending.back();
        pending.pop_back();
        json node = std::move(entities[index]);
        node.erase("siblingIndex");
        ordered.push_back(std::move(node));
        pending.insert(pending.end(), children[index].rbegin(), children[index].rend());
    }
    entities = std::move(ordered);
}

} // namespace

namespace Detail {

std::optional<std::uint64_t> ParsePrefabLocalId(std::string_view text) {
    std::uint64_t id = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
    return error == std::errc() && end == text.data() + text.size() && id != 0 ? std::optional(id) : std::nullopt;
}

json KeyPrefabEntity(const json& saved) {
    json node = saved;
    for (const char* field :
         {"id", "parent", "siblingIndex", "prefabRoot", "prefabLocal", "prefabAsset", "referenceScope"}) {
        node.erase(field);
    }
    json components = json::object();
    json order = json::array();
    const auto list = saved.find("components");
    if (list != saved.end() && list->is_array()) {
        for (const json& component : *list) {
            if (component.is_object() && component.contains("id") && component["id"].is_string()) {
                const auto id = component["id"].get<std::string>();
                components[id] = component;
                order.push_back(id);
            }
        }
    }
    node["components"] = std::move(components);
    node["componentOrder"] = std::move(order);
    return node;
}

void UnkeyPrefabEntity(json& node) {
    json components = json::array();
    std::unordered_set<std::string> placed;
    const auto order = node.find("componentOrder");
    if (order != node.end() && order->is_array()) {
        for (const json& entry : *order) {
            if (entry.is_string()) {
                const auto id = entry.get<std::string>();
                if (node["components"].contains(id) && placed.insert(id).second) {
                    components.push_back(node["components"][id]);
                }
            }
        }
    }
    if (node["components"].is_object()) {
        for (const auto& [id, component] : node["components"].items()) {
            if (placed.insert(id).second) {
                components.push_back(component);
            }
        }
    }
    node["components"] = std::move(components);
    node.erase("componentOrder");
}

bool HasPrefabOverrideTarget(const json& node, const json& patch) {
    if (!patch.is_object() || !patch.contains("path") || !patch["path"].is_string()) {
        return false;
    }
    const std::string path = patch["path"].get<std::string>();
    constexpr std::string_view Prefix = "/components/";
    if (!path.starts_with(Prefix)) {
        return true;
    }
    const std::size_t end = path.find('/', Prefix.size());
    const std::string id = path.substr(Prefix.size(), end - Prefix.size());
    return node["components"].contains(id) || (end == std::string::npos && patch.value("op", "") == "add");
}

bool ApplyPrefabOverrides(json& node, const json& patches, AssetId prefab, std::uint64_t local) {
    if (!patches.is_array()) {
        return false;
    }
    for (const json& patch : patches) {
        if (!patch.is_object() || !patch.contains("op") || !patch["op"].is_string() || !patch.contains("path") ||
            !patch["path"].is_string()) {
            return false;
        }
        const std::string op = patch["op"].get<std::string>();
        const std::string path = patch["path"].get<std::string>();
        if ((op != "add" && op != "replace" && op != "remove") || path.empty() || path.front() != '/' ||
            ((op == "add" || op == "replace") && !patch.contains("value"))) {
            return false;
        }
        if (!HasPrefabOverrideTarget(node, patch)) {
            Log::GetEngineLogger().Warning("Prefab {} entity {} no longer has override target '{}'; it is dropped",
                                           prefab.ToString(), local, path);
            continue;
        }
        try {
            const json::json_pointer pointer(path);
            // Older scenes patched the markers of references, which prefabs keep as plain ids.
            const std::string key = pointer.back();
            if ((key == "prefabEntity" || key == "sceneEntity") && node.contains(pointer.parent_pointer()) &&
                !node[pointer.parent_pointer()].is_object()) {
                if (op != "remove") {
                    node[pointer.parent_pointer()] = {{key, patch["value"]}};
                }
                continue;
            }
            if (op == "remove") {
                if (node.contains(pointer)) {
                    node = node.patch(json::array({patch}));
                }
            } else {
                // A field added to game code need not have existed in an older source document.
                node[pointer] = patch["value"];
            }
        } catch (const json::exception&) {
            Log::GetEngineLogger().Warning("Prefab {} entity {} could not apply override '{}'", prefab.ToString(),
                                           local, path);
        }
    }
    return true;
}

void ForEachEntityMarker(json& value, const std::function<void(json& marker)>& fn) {
    if (value.is_object() && value.size() == 1 && (value.contains("prefabEntity") || value.contains("sceneEntity"))) {
        fn(value);
        return;
    }
    if (value.is_structured()) {
        for (json& child : value) {
            ForEachEntityMarker(child, fn);
        }
    }
}

} // namespace Detail

std::optional<Prefab> Prefab::FromDocument(Detail::PrefabDocument prefab) {
    json& document = prefab.document;
    if (!document.is_object() || !document.contains("format") || document["format"] != "spot-scene" ||
        !document.contains("version") || !document["version"].is_number_integer() ||
        (document["version"] != 1 && document["version"] != 2) || !document.contains("entities") ||
        !ValidateTree(document["entities"])) {
        Log::GetEngineLogger().Warning(
            "Could not load the prefab: expected a saved scene containing one entity tree with distinct, nonzero ids");
        return std::nullopt;
    }
    document["version"] = 2;
    prefab.dependencies.clear();
    prefab.flat = true;
    for (const json& entity : document["entities"]) {
        if (entity.contains("prefab")) {
            const auto id = *AssetId::Parse(entity["prefab"].get<std::string>());
            if (std::ranges::find(prefab.dependencies, id) == prefab.dependencies.end()) {
                prefab.dependencies.push_back(id);
            }
            prefab.flat = false;
        }
        if (entity.contains("siblingIndex")) {
            prefab.flat = false;
        }
    }
    return Prefab(std::make_shared<const Detail::PrefabDocument>(std::move(prefab)));
}

std::optional<Prefab> Prefab::FromString(std::string_view text) {
    return FromDocument({.document = json::parse(text.begin(), text.end(), nullptr, false)});
}

std::optional<Prefab> Prefab::Flatten(const NestedPrefabs& nested) const {
    if (m_document->flat) {
        return *this;
    }
    json document = m_document->document;
    if (!Detail::SceneSerializer::ExpandPrefabs(document, nested, nullptr)) {
        return std::nullopt;
    }
    for (json& node : document["entities"]) {
        node.erase("prefabRoot");
        node.erase("prefabLocal");
        node.erase("prefabAsset");
    }
    if (!ValidateTree(document["entities"])) {
        Log::GetEngineLogger().Warning("Could not flatten the prefab: its expanded entities are not one tree");
        return std::nullopt;
    }
    NormalizeOrder(document["entities"]);
    return FromDocument({.document = std::move(document)});
}

bool Prefab::IsFlat() const {
    return m_document->flat;
}

std::string Prefab::GetEntityText() const {
    return m_document->document.dump(2, ' ', false, json::error_handler_t::replace);
}

std::span<const AssetId> Prefab::GetDependencies() const {
    return m_document->dependencies;
}

} // namespace Spot
