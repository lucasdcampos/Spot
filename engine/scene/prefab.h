#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Spot {

class Entity;

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
                                            bool apply = false);
// Expands just this instance in its scene; other instances keep their links and overrides.
std::optional<std::string> UnpackPrefabToString(const Entity& root);

// An immutable entity tree in the saved scene format. Components are created only when the prefab is instantiated,
// so game code can register their types after assets load.
class Prefab {
public:
    // Accepts exactly one tree with distinct, nonzero entity ids. An external parent of its root is discarded,
    // so Entity::SaveToString(true) can be used even when the entity belongs to a larger hierarchy.
    // Nothing, after logging why, when the document or hierarchy is invalid.
    static std::optional<Prefab> FromString(std::string_view text);

    const std::string& GetEntityText() const { return m_entityText; }

private:
    explicit Prefab(std::string entityText) : m_entityText(std::move(entityText)) {}

    std::string m_entityText;
};

} // namespace Spot
