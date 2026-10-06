#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Spot {

class AssetManager;
class Component;
class Entity;

// Single components as JSON, in the form scenes save them: type, enabled state and fields. Tools use them to copy a
// component's values, or to take snapshots to undo changes. References to entities are kept as they are.

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
