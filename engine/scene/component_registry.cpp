#include "scene/component_registry.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace Spot::Detail {

namespace {

struct Registry {
    std::mutex mutex;
    std::unordered_map<std::type_index, std::string> names;
    std::unordered_map<std::string, ComponentFactory> factories;
    std::unordered_map<std::string, std::type_index> types;
};

// Built on first use, so components can register from static initializers in any order.
Registry& GetRegistry() {
    static Registry registry;
    return registry;
}

} // namespace

void RegisterComponentType(std::type_index type, std::string name, ComponentFactory factory) {
    Registry& registry = GetRegistry();
    const std::lock_guard lock(registry.mutex);

    // The name stands for a single type, and a type for a single name.
    if (const auto previous = registry.types.find(name); previous != registry.types.end()) {
        registry.names.erase(previous->second);
        registry.types.erase(previous);
    }
    if (const auto previous = registry.names.find(type); previous != registry.names.end()) {
        registry.factories.erase(previous->second);
        registry.types.erase(previous->second);
    }

    registry.names.insert_or_assign(type, name);
    registry.factories.insert_or_assign(name, factory);
    registry.types.insert_or_assign(std::move(name), type);
}

const std::string* FindComponentTypeName(std::type_index type) {
    Registry& registry = GetRegistry();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.names.find(type);
    // Entries are never moved once inserted, so the pointer stays valid until the type registers again.
    return found != registry.names.end() ? &found->second : nullptr;
}

ComponentFactory FindComponentFactory(std::string_view name) {
    Registry& registry = GetRegistry();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.factories.find(std::string(name));
    return found != registry.factories.end() ? found->second : nullptr;
}

} // namespace Spot::Detail
