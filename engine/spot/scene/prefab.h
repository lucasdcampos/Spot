#pragma once

#include "spot/assets/asset_manager.h"
#include "spot/core/ids/asset_id.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Spot {

class Entity;
class AssetManager;

// Zero selects the entire instance; otherwise the source-local entity and its JSON-pointer subtree are selected.
struct PrefabOverrideScope {
    std::uint64_t entity = 0;
    std::string pointer;
};

struct PrefabOverride {
    PrefabOverrideScope scope;
    std::string operation;
};

// Prepared documents let tools write the asset and replace the scene as one undoable operation.
struct PrefabEdit {
    std::string sceneText;
    // Empty for Revert. Apply produces a validated source tree with source-local references.
    std::string prefabText;
};

std::vector<PrefabOverride> GetPrefabOverrides(const Entity& root);
std::optional<PrefabEdit> PreparePrefabEdit(const Entity& root, const PrefabOverrideScope& scope = {},
                                            bool apply = false, AssetManager* assets = nullptr);
// Expands just this instance in its scene; other instances keep their links and overrides.
std::optional<std::string> UnpackPrefabToString(const Entity& root);

// An immutable entity tree in the saved scene format. Components are created only when the prefab is instantiated,
// so game code can register their types after assets load.
class Prefab {
public:
    // Accepts exactly one tree with distinct, nonzero entity ids. An external parent of its root is discarded,
    // so Entity::SaveToString() can be used even when the entity belongs to a larger hierarchy. Nested prefab
    // records are kept; their dependencies and expanded tree are checked when instantiated.
    // Nothing, after logging why, when the document or hierarchy is invalid.
    static std::optional<Prefab> FromString(std::string_view text);

    const std::string& GetEntityText() const { return m_entityText; }
    std::span<const AssetId> GetDependencies() const { return m_dependencies; }

private:
    Prefab(std::string entityText, std::vector<AssetId> dependencies)
        : m_entityText(std::move(entityText)), m_dependencies(std::move(dependencies)) {}

    std::string m_entityText;
    std::vector<AssetId> m_dependencies;
};

template <>
struct AssetTypeOf<Prefab> {
    static constexpr AssetType Value = AssetType::Prefab;
};

} // namespace Spot
