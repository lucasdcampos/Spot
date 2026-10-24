#pragma once

// Engine-internal: a pak holds a game's imported assets in one file, "assets.pak" in its import directory, in place of
// a file per asset, which opening hundreds of costs more than reading them. It starts with a table of the assets it
// holds, each compressed with LZ4 when that makes it smaller, then their data.

#include "spot/core/ids/asset_id.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Spot::Detail {

inline constexpr const char* AssetPakFileName = "assets.pak";

class AssetPak {
public:
    // Null, after logging why, when the file is not a pak this engine reads.
    static std::unique_ptr<AssetPak> Open(const std::filesystem::path& path);

    bool Contains(AssetId id) const { return m_entries.contains(id); }
    std::size_t GetAssetCount() const { return m_entries.size(); }
    // The asset's file as it was imported, header included; safe to call from several threads at once.
    std::optional<std::vector<std::byte>> Read(AssetId id) const;

private:
    struct Entry {
        std::uint64_t offset;
        std::uint64_t storedSize;
        std::uint64_t size;
        bool compressed;
    };

    mutable std::ifstream m_file;
    mutable std::mutex m_mutex;
    std::unordered_map<AssetId, Entry> m_entries;
};

// Writes the imported assets of the directory, every "<id>.asset" in it, into a pak at the path.
bool WriteAssetPak(const std::filesystem::path& path, const std::filesystem::path& importDirectory);

} // namespace Spot::Detail
