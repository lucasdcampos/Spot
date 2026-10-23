#pragma once

#include <cstdint>

namespace Spot {

// Identifies an entity. Unlike a pointer, it can be kept after the entity is destroyed: Scene::GetEntity() then
// returns null. Ids are unique across all scenes, never reused, and kept when an entity moves to another scene.
enum class EntityId : std::uint64_t { Invalid = 0 };

} // namespace Spot
