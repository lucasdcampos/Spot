#pragma once

#include "scene/component.h"

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>

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
    Detail::RegisterComponentType(typeid(T), std::move(name),
                                  []() -> std::unique_ptr<Component> { return std::make_unique<T>(); });
}

template <std::derived_from<Component> T>
bool IsComponentRegistered() {
    return Detail::FindComponentTypeName(typeid(T)) != nullptr;
}

} // namespace Spot
