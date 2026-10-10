#include "scene/prefab.h"

#include "core/log.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
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

    std::optional<std::size_t> root;
    std::vector<std::vector<std::size_t>> children(entities.size());
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
    if (visited != entities.size()) {
        return false;
    }
    entities[*root].erase("parent");
    return true;
}

} // namespace

std::optional<Prefab> Prefab::FromString(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object() || !document.contains("format") || document["format"] != "spot-scene" ||
        !document.contains("version") || !document["version"].is_number_integer() || document["version"] != 1 ||
        !document.contains("entities") || !ValidateTree(document["entities"])) {
        Log::GetEngineLogger().Warning(
            "Could not load the prefab: expected a saved scene containing one entity tree with distinct, nonzero ids");
        return std::nullopt;
    }
    return Prefab(document.dump(2, ' ', false, json::error_handler_t::replace));
}

} // namespace Spot
