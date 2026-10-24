#pragma once

// Engine-internal: the JSON behind an Archive. Public headers never include it, so the JSON library stays private.

#include "spot/core/io/archive.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace Spot::Detail {

struct ArchiveState;

// Saves and loads entity references, which the scene remaps as it loads copies of entities. Without one, an
// archive writes and reads them as plain numbers.
class EntityReferenceCodec {
public:
    virtual ~EntityReferenceCodec() = default;

    virtual void Field(ArchiveState& state, std::string_view name, EntityId& value) const = 0;
};

struct ArchiveState {
    bool loading = false;
    // The node fields are read from or written to is the last one.
    std::vector<nlohmann::json*> nodes;
    // Loads asset references; the running application's when null.
    AssetManager* assets = nullptr;
    const EntityReferenceCodec* entities = nullptr;
};

// The named field of the current node, or the node itself for an empty name; null when loading finds none.
nlohmann::json* FindArchiveField(ArchiveState& state, std::string_view name);
nlohmann::json& AddArchiveField(ArchiveState& state, std::string_view name);
void WarnArchiveFieldType(std::string_view name, const char* expected);

} // namespace Spot::Detail
