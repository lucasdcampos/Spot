#pragma once

#include <cstdint>
#include <typeindex>
#include <typeinfo>

namespace Spot {

class Component;

namespace Detail {

// A small number per component type, given the first time the type is seen, so queries compare numbers instead of
// casting every component they visit. Types are told apart by their type_info, so a type keeps its number when the
// game module that defines it is reloaded.
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

} // namespace Detail

} // namespace Spot
