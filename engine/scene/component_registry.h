#pragma once

#include "scene/component.h"
#include "scene/component_requirements.h"

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace Spot {

namespace Detail {

using ComponentFactory = std::unique_ptr<Component> (*)();

void RegisterComponentType(std::type_index type, std::string name, ComponentFactory factory);
// Null when the type is not registered.
const std::string* FindComponentTypeName(std::type_index type);
ComponentFactory FindComponentFactory(std::string_view name);

} // namespace Detail

// Lets scenes save and load components of this type, under a name that is written to the files and must stay the
// same for them to load. Components of types that are not registered are left out when saving, with a warning.
// Registering a name again replaces the type it stands for.
//
//     Spot::RegisterComponent<PlayerController>("PlayerController");
template <std::derived_from<Component> T>
    requires std::default_initializable<T>
void RegisterComponent(std::string name) {
    // So components made from the name, as tools and saved scenes make them, know what they require too.
    Detail::DeclareComponentRequirements<T>();
    Detail::RegisterComponentType(typeid(T), std::move(name),
                                  []() -> std::unique_ptr<Component> { return std::make_unique<T>(); });
}

template <std::derived_from<Component> T>
bool IsComponentRegistered() {
    return Detail::FindComponentTypeName(typeid(T)) != nullptr;
}

// Forgets the type registered under the name, such as when the code that defines it is unloaded. Scenes keep
// components of types that are not registered as MissingComponents.
void UnregisterComponent(std::string_view name);

// The names component types are registered under, sorted, the engine's own included.
std::vector<std::string> GetComponentTypeNames();

// A new component of the type registered under the name, not added to any entity yet; null when no type has it.
std::unique_ptr<Component> CreateComponent(std::string_view name);

// The name the component's type is registered under, or, for a MissingComponent, the name its type was saved under.
// Empty when it is not registered.
std::string GetComponentTypeName(const Component& component);

} // namespace Spot
