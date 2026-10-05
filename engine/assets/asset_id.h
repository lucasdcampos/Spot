#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Spot {

// Identifies an asset for good: it stays the same when the asset's file is renamed or moved, so references to the
// asset keep working. Ids are random 128-bit values, written as 32 hexadecimal digits.
struct AssetId {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    static AssetId Generate();
    // Nothing when the text is not 32 hexadecimal digits.
    static std::optional<AssetId> Parse(std::string_view text);

    // The default id is null and refers to no asset.
    bool IsValid() const { return high != 0 || low != 0; }
    std::string ToString() const;

    friend constexpr bool operator==(const AssetId&, const AssetId&) = default;
    friend constexpr auto operator<=>(const AssetId&, const AssetId&) = default;
};

} // namespace Spot

template <>
struct std::hash<Spot::AssetId> {
    std::size_t operator()(const Spot::AssetId& id) const noexcept {
        // Ids are random, so mixing the halves is enough.
        return std::hash<std::uint64_t>{}(id.high ^ (id.low * 0x9E3779B97F4A7C15ull));
    }
};
