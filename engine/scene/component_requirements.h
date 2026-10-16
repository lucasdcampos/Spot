#pragma once

#include "scene/component_type.h"

#include <memory>
#include <vector>

namespace Spot {

class Component;
class Entity;

// The components a component needs on its entity, declared in its class with SPOT_REQUIRE_COMPONENTS().
template <typename... Components>
struct RequiredComponentList {};

namespace Detail {

struct ComponentRequirement {
    ComponentTypeIndex type = 0;
    // Whether the entity has a component of the type, or of one derived from it.
    bool (*isPresent)(const Entity& entity) = nullptr;
    // Whether the component is of the type, or of one derived from it.
    bool (*matches)(const Component& component) = nullptr;
    std::unique_ptr<Component> (*create)() = nullptr;
};

void SetComponentRequirements(ComponentTypeIndex type, std::vector<ComponentRequirement> requirements);
// Null when components of the type need none, or their type never declared them.
const std::vector<ComponentRequirement>* FindComponentRequirements(ComponentTypeIndex type);

// Reads the requirements a class declared, wherever in it SPOT_REQUIRE_COMPONENTS() was written.
struct ComponentRequirementsAccess {
    template <typename T>
    static constexpr bool Declares = requires { typename T::RequiredComponents; };

    template <typename T>
    using Of = typename T::RequiredComponents;
};

// Records what T requires, the first time it is called for T; types that require nothing record nothing. Defined in
// entity.h, which can tell whether an entity has a component.
template <typename T>
void DeclareComponentRequirements();

} // namespace Detail

} // namespace Spot

// Makes adding the component, by code or by tools, add the components of these types first when its entity has none
// of them, and keeps them from being removed while it is there. Written in the class:
//
//     class Player final : public Spot::Component {
//         SPOT_REQUIRE_COMPONENTS(Health, PlayerMovement)
//         ...
//     };
//
// The required types must be default-constructible. Loading a saved scene adds nothing: it has what was saved.
#define SPOT_REQUIRE_COMPONENTS(...)                                                                                   \
    using RequiredComponents = ::Spot::RequiredComponentList<__VA_ARGS__>;                                             \
    friend struct ::Spot::Detail::ComponentRequirementsAccess;
