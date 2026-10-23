#include "spot/core/asset_id.h"

#include <format>
#include <random>

namespace Spot {

AssetId AssetId::Generate() {
    thread_local std::mt19937_64 generator = [] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }();

    AssetId id;
    do {
        id = {generator(), generator()};
    } while (!id.IsValid());
    return id;
}

std::optional<AssetId> AssetId::Parse(std::string_view text) {
    if (text.size() != 32) {
        return std::nullopt;
    }

    AssetId id;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        std::uint64_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint64_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint64_t>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        std::uint64_t& half = i < 16 ? id.high : id.low;
        half = (half << 4) | digit;
    }
    return id;
}

std::string AssetId::ToString() const {
    return std::format("{:016x}{:016x}", high, low);
}

} // namespace Spot
