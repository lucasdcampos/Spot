#pragma once

#include "spot/assets/asset_manager.h"
#include "spot/core/ids/asset_id.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Spot {

namespace Detail {
struct PrefabDocument;
} // namespace Detail

// An immutable entity tree in the saved scene format, parsed once when it loads. Components are created only when
// the prefab is instantiated, so game code can register their types after assets load.
//
// Imported prefabs are flat: the prefabs nested in their source are expanded into their tree when they are imported,
// so games instantiate a prefab without resolving others.
class Prefab {
public:
    // Finds a nested prefab by its id; empty when it is unavailable.
    using NestedPrefabs = std::function<AssetRef<Prefab>(AssetId)>;

    // Accepts exactly one tree with distinct, nonzero entity ids. An external parent of its root is discarded,
    // so Entity::SaveToString() can be used even when the entity belongs to a larger hierarchy. Nested prefab
    // records are kept; their dependencies and expanded tree are checked when flattened.
    // Nothing, after logging why, when the document or hierarchy is invalid.
    static std::optional<Prefab> FromString(std::string_view text);

    // The prefab with the trees of the prefabs nested in it expanded into its own and their overrides applied. The
    // entity references of a nested tree keep pointing at its own entities. Nothing, after logging why, when a
    // nested prefab is unavailable, nests itself or its records are invalid.
    std::optional<Prefab> Flatten(const NestedPrefabs& nested) const;
    // Whether the tree has no nested prefab records, as instantiating needs.
    bool IsFlat() const;

    std::string GetEntityText() const;
    // The prefabs nested in the tree; none once flattened.
    std::span<const AssetId> GetDependencies() const;
    // Engine-internal: the parsed tree.
    const Detail::PrefabDocument& GetDocument() const { return *m_document; }

private:
    explicit Prefab(std::shared_ptr<const Detail::PrefabDocument> document) : m_document(std::move(document)) {}

    // Validates the document's tree, which FromString() and Flatten() make.
    static std::optional<Prefab> FromDocument(Detail::PrefabDocument document);

    std::shared_ptr<const Detail::PrefabDocument> m_document;
};

template <>
struct AssetTypeOf<Prefab> {
    static constexpr AssetType Value = AssetType::Prefab;
};

} // namespace Spot
