#pragma once

// Engine-internal, shared with the tools: how prefabs and their instances are saved, and how overrides apply.
//
// A scene file saves a prefab instance as a record: the prefab, the saved ids of its entities by their ids in the
// prefab ("entityIds"), JSON patches of those entities ("overrides") and the entities it removed. Loading expands the
// record into the entities, each of which then saves with "prefabRoot" and "prefabLocal", and the root with
// "prefabAsset", which is how Scene::SaveToString() keeps instances: entity references inside an instance save as
// {"prefabEntity": id in the prefab}, and those to other entities as {"sceneEntity": id}. The tools turn such entities
// back into records.
//
// Overrides address an entity in its keyed form: saved without its id and links, its components by id with their
// order apart, and, below the instance root, its parent as a marker and its sibling index.

#include "spot/core/ids/asset_id.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace Spot::Detail {

struct PrefabDocument {
    nlohmann::json document;
    std::vector<AssetId> dependencies;
    // No nested prefab records, and the entities saved in their sibling order.
    bool flat = false;
};

std::optional<std::uint64_t> ParsePrefabLocalId(std::string_view text);
// The keyed form of a saved entity, apart from its parent and sibling index, which depend on where it is.
nlohmann::json KeyPrefabEntity(const nlohmann::json& node);
// Back to the saved form's list of components.
void UnkeyPrefabEntity(nlohmann::json& node);
// Applies JSON patches ("add", "replace" or "remove") to a keyed entity. Targets it no longer has are skipped with
// a warning, as the prefab may have changed since. False when the patches are malformed.
bool ApplyPrefabOverrides(nlohmann::json& node, const nlohmann::json& patches, AssetId prefab, std::uint64_t local);
// Whether the patch addresses something the keyed entity has, or adds a whole component.
bool HasPrefabOverrideTarget(const nlohmann::json& node, const nlohmann::json& patch);
// Calls fn on each entity reference marker in the value: {"prefabEntity": id} or {"sceneEntity": id}.
void ForEachEntityMarker(nlohmann::json& value, const std::function<void(nlohmann::json& marker)>& fn);

} // namespace Spot::Detail
