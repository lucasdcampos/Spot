#pragma once

#include <cstdint>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace Spot {

class Component;

namespace Detail {

// A small number per component type, given the first time the type is seen, so queries compare numbers instead of
// casting every component they visit. Types are told apart by their name, so a type keeps its number when the game
// module that defines it is reloaded.
using ComponentTypeIndex = std::uint32_t;

ComponentTypeIndex GetComponentTypeIndex(std::type_index type);

template <typename T>
ComponentTypeIndex GetComponentTypeIndex() {
    static const ComponentTypeIndex index = GetComponentTypeIndex(typeid(T));
    return index;
}

// Whether components of the concrete type are also of the queried one, as a base class or the type itself: known
// (1 or 0) once a cast answered it, -1 before. The table is only used from the thread that owns the scenes.
signed char& GetComponentTypeRelation(ComponentTypeIndex concrete, ComponentTypeIndex queried);

// The update hooks the scene calls every frame or step, which a type may leave as Component's own, empty ones.
enum class ComponentHook : std::uint8_t {
    FixedUpdate = 1,
    Update = 2,
    LateUpdate = 4,
};

// A bit per hook that components of the type were found not to override, by Component's own hooks, which mark it the
// first time they are called. The scene then skips those hooks for every component of the type.
std::vector<std::uint8_t>& GetUnusedComponentHooks();

inline bool IsComponentHookUnused(ComponentTypeIndex type, ComponentHook hook) {
    const std::vector<std::uint8_t>& unused = GetUnusedComponentHooks();
    return type < unused.size() && (unused[type] & static_cast<std::uint8_t>(hook)) != 0;
}

void MarkComponentHookUnused(ComponentTypeIndex type, ComponentHook hook);

} // namespace Detail

} // namespace Spot
