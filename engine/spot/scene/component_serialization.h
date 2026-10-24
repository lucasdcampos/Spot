#pragma once

#include "spot/core/ids/asset_id.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Spot {

class AssetManager;
class Archive;
class Component;
class Entity;

// Single components as JSON, in the form scenes save them: type, enabled state and fields. Tools use them to copy a
// component's values, or to take snapshots to undo changes. References to entities are kept as they are.

// Runs the component's Serialize() with the archive, such as one whose backend draws an inspector.
void SerializeComponent(Component& component, Archive& archive);
// Calls the component's OnValidate(), as tools do after changing its fields.
void ValidateComponent(Component& component);

// Loads the component's references to these assets again, through the asset manager, or the running application's
// when it is null, so it gets their new versions after AssetManager::Reload(). Its other fields are left alone.
void ReloadAssetReferences(Component& component, std::span<const AssetId> assets, AssetManager* manager = nullptr);

// Empty when the component's type is not registered.
std::string SaveComponentToString(const Component& component);

// Replaces the component's fields and enabled state with saved ones of the same type. Fields the text lacks keep
// their values. Returns false, changing nothing, when the text is not valid or is for another type.
bool LoadComponentFromString(Component& component, std::string_view text, AssetManager* assets = nullptr);

// Adds a component of the saved type, with the saved fields, at that place among the entity's components or last; a
// MissingComponent that keeps them when the type is not registered. Returns null, adding nothing, when the text is
// not valid.
Component* AddComponentFromString(Entity& entity, std::string_view text,
                                  std::optional<std::size_t> index = std::nullopt, AssetManager* assets = nullptr);

} // namespace Spot
