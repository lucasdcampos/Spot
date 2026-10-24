#include "spot/scene/component_registry.h"

#include "spot/scene/missing_component.h"

#include <algorithm>
#include <atomic>
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

// Built on first use so static component registrations do not depend on initialization order.
Registry& GetRegistry() {
    static Registry registry;
    return registry;
}

std::atomic<void (*)(std::type_index)> s_typeCleanup = nullptr;
void CleanupType(std::type_index type) {
    if (const auto cleanup = s_typeCleanup.load()) {
        cleanup(type);
    }
}

} // namespace

void SetComponentTypeCleanup(void (*cleanup)(std::type_index)) {
    s_typeCleanup.store(cleanup);
}

void RegisterComponentType(std::type_index type, std::string name, ComponentFactory factory) {
    // Code that registers types may be a reloaded game module, whose types may override hooks they did not before.
    GetUnusedComponentHooks().clear();
    Registry& registry = GetRegistry();
    const std::lock_guard lock(registry.mutex);

    // The name stands for a single type, and a type for a single name.
    if (const auto previous = registry.types.find(name); previous != registry.types.end()) {
        CleanupType(previous->second);
        registry.names.erase(previous->second);
        registry.types.erase(previous);
    }
    if (const auto previous = registry.names.find(type); previous != registry.names.end()) {
        CleanupType(type);
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

namespace Spot {

void UnregisterComponent(std::string_view name) {
    Detail::GetUnusedComponentHooks().clear();
    Detail::Registry& registry = Detail::GetRegistry();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.types.find(std::string(name));
    if (found == registry.types.end()) {
        return;
    }
    // What the type requires refers to code of the module that defined it, which may be about to be unloaded; a
    // module loaded again declares it anew.
    Detail::SetComponentRequirements(Detail::GetComponentTypeIndex(found->second), {});
    Detail::CleanupType(found->second);
    registry.names.erase(found->second);
    registry.factories.erase(found->first);
    registry.types.erase(found);
}

std::vector<std::string> GetComponentTypeNames() {
    Detail::Registry& registry = Detail::GetRegistry();
    const std::lock_guard lock(registry.mutex);
    std::vector<std::string> names;
    names.reserve(registry.types.size());
    for (const auto& [name, type] : registry.types) {
        names.push_back(name);
    }
    std::ranges::sort(names);
    return names;
}

std::unique_ptr<Component> CreateComponent(std::string_view name) {
    const Detail::ComponentFactory factory = Detail::FindComponentFactory(name);
    return factory ? factory() : nullptr;
}

std::string GetComponentTypeName(const Component& component) {
    if (const auto* missing = dynamic_cast<const MissingComponent*>(&component)) {
        return missing->GetTypeName();
    }
    const std::string* name = Detail::FindComponentTypeName(typeid(component));
    return name ? *name : std::string();
}

} // namespace Spot
