#include "scene/component_type.h"

#include "scene/component_requirements.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace Spot::Detail {

namespace {

struct TypeTable {
    std::mutex mutex;
    std::unordered_map<std::type_index, ComponentTypeIndex> indices;
    // relations[concrete][queried]: -1 unknown, 0 no, 1 yes.
    std::vector<std::vector<signed char>> relations;
};

TypeTable& GetTable() {
    // Never freed, so components destroyed by static destructors can still be looked up.
    static TypeTable* table = new TypeTable();
    return *table;
}

} // namespace

ComponentTypeIndex GetComponentTypeIndex(std::type_index type) {
    TypeTable& table = GetTable();
    const std::scoped_lock lock(table.mutex);
    const auto [found, added] = table.indices.try_emplace(type, static_cast<ComponentTypeIndex>(table.indices.size()));
    return found->second;
}

signed char& GetComponentTypeRelation(ComponentTypeIndex concrete, ComponentTypeIndex queried) {
    auto& relations = GetTable().relations;
    if (concrete >= relations.size()) {
        relations.resize(concrete + 1);
    }
    auto& row = relations[concrete];
    if (queried >= row.size()) {
        row.resize(queried + 1, -1);
        // A type is always itself.
        if (concrete < row.size()) {
            row[concrete] = 1;
        }
    }
    return row[queried];
}

namespace {

std::vector<std::vector<ComponentRequirement>>& GetRequirements() {
    static auto* requirements = new std::vector<std::vector<ComponentRequirement>>();
    return *requirements;
}

} // namespace

void SetComponentRequirements(ComponentTypeIndex type, std::vector<ComponentRequirement> requirements) {
    auto& all = GetRequirements();
    if (type >= all.size()) {
        all.resize(type + 1);
    }
    all[type] = std::move(requirements);
}

const std::vector<ComponentRequirement>* FindComponentRequirements(ComponentTypeIndex type) {
    const auto& all = GetRequirements();
    return type < all.size() && !all[type].empty() ? &all[type] : nullptr;
}

std::vector<std::uint8_t>& GetUnusedComponentHooks() {
    static auto* unused = new std::vector<std::uint8_t>();
    return *unused;
}

void MarkComponentHookUnused(ComponentTypeIndex type, ComponentHook hook) {
    std::vector<std::uint8_t>& unused = GetUnusedComponentHooks();
    if (type >= unused.size()) {
        unused.resize(type + 1, 0);
    }
    unused[type] |= static_cast<std::uint8_t>(hook);
}

} // namespace Spot::Detail
