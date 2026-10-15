#include "scene/prefab.h"

#include "core/asset_id.h"
#include "core/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
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

} // namespace

std::optional<Prefab> Prefab::FromString(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object() || !document.contains("format") || document["format"] != "spot-scene" ||
        !document.contains("version") || !document["version"].is_number_integer() ||
        (document["version"] != 1 && document["version"] != 2) || !document.contains("entities") ||
        !ValidateTree(document["entities"])) {
        Log::GetEngineLogger().Warning(
            "Could not load the prefab: expected a saved scene containing one entity tree with distinct, nonzero ids");
        return std::nullopt;
    }
    document["version"] = 2;
    std::vector<AssetId> dependencies;
    for (const json& entity : document["entities"]) {
        if (entity.contains("prefab")) {
            const auto id = *AssetId::Parse(entity["prefab"].get<std::string>());
            if (std::ranges::find(dependencies, id) == dependencies.end()) {
                dependencies.push_back(id);
            }
        }
    }
    return Prefab(document.dump(2, ' ', false, json::error_handler_t::replace), std::move(dependencies));
}

} // namespace Spot
