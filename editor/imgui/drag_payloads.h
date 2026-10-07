#pragma once

#include <spot.h>

#include <imgui.h>

#include <cstddef>
#include <span>

// What windows drag to each other.
namespace Spot::Editor {

// Entities of the active scene, as EntityId values, such as from the hierarchy.
inline constexpr const char* EntitiesPayload = "SPOT_ENTITIES";
// Assets of the project, as AssetId values, such as from the asset browser.
inline constexpr const char* AssetsPayload = "SPOT_ASSETS";

inline std::span<const EntityId> GetPayloadEntities(const ImGuiPayload& payload) {
    return {static_cast<const EntityId*>(payload.Data), static_cast<std::size_t>(payload.DataSize) / sizeof(EntityId)};
}

inline std::span<const AssetId> GetPayloadAssets(const ImGuiPayload& payload) {
    return {static_cast<const AssetId*>(payload.Data), static_cast<std::size_t>(payload.DataSize) / sizeof(AssetId)};
}

} // namespace Spot::Editor
