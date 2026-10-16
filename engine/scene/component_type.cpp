#include "scene/component_type.h"

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

} // namespace Spot::Detail
