#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Spot {

// An immutable entity tree in the saved scene format. Components are created only when the prefab is instantiated,
// so game code can register their types after assets load.
class Prefab {
public:
    // Accepts exactly one tree with distinct, nonzero entity ids. An external parent of its root is discarded,
    // so Entity::SaveToString(true) can be used even when the entity belongs to a larger hierarchy.
    // Nothing, after logging why, when the document or hierarchy is invalid.
    static std::optional<Prefab> FromString(std::string_view text);

    const std::string& GetEntityText() const { return m_entityText; }

private:
    explicit Prefab(std::string entityText) : m_entityText(std::move(entityText)) {}

    std::string m_entityText;
};

} // namespace Spot
