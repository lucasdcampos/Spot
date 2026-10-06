#pragma once

// Engine-internal: the JSON behind an Archive. Public headers never include it, so the JSON library stays private.

#include "scene/entity.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Spot {
class AssetManager;
class Scene;
} // namespace Spot

namespace Spot::Detail {

struct ArchiveState {
    bool loading = false;
    // The node fields are read from or written to is the last one.
    std::vector<nlohmann::json*> nodes;
    // When loading, maps the saved ids of the entities being loaded to their new ids. Without it, ids load as saved.
    const std::unordered_map<std::uint64_t, EntityId>* entityIds = nullptr;
    // With entityIds, ids missing from them that this scene has an entity for are kept rather than lost.
    const Scene* scene = nullptr;
    // Loads asset references; the running application's when null.
    AssetManager* assets = nullptr;
};

} // namespace Spot::Detail
