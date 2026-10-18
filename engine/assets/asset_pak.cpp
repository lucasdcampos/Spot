#include "assets/asset_pak.h"

#include "assets/asset_file.h"
#include "core/file_io.h"
#include "core/log.h"
#include "core/paths.h"

#include <lz4.h>

#include <algorithm>
#include <array>
#include <limits>

namespace Spot::Detail {

namespace {

constexpr std::array<char, 4> PakMagic{'S', 'P', 'A', 'K'};
// Bumped when the layout changes.
constexpr std::uint32_t PakVersion = 1;

// Of each entry in the table: the id's two halves, then the offset, stored size and size, then whether it is
// compressed.
constexpr std::size_t EntrySize = 2 * 8 + 3 * 8 + 4;

} // namespace

std::unique_ptr<AssetPak> AssetPak::Open(const std::filesystem::path& path) {
    auto pak = std::unique_ptr<AssetPak>(new AssetPak());
    pak->m_file.open(path, std::ios::binary);
    if (!pak->m_file) {
        Log::GetEngineLogger().Error("Could not open the asset pak '{}'", PathToUtf8(path));
        return nullptr;
    }
    std::array<std::byte, 12> header{};
    pak->m_file.read(reinterpret_cast<char*>(header.data()), header.size());
    BinaryReader headerReader(header);
    const auto magic = headerReader.Read<std::array<char, 4>>();
    const auto version = headerReader.Read<std::uint32_t>();
    const auto count = headerReader.Read<std::uint32_t>();
    if (!pak->m_file || magic != PakMagic || version != PakVersion) {
        Log::GetEngineLogger().Error("'{}' is not an asset pak of version {}", PathToUtf8(path), PakVersion);
        return nullptr;
    }
    std::vector<std::byte> table(static_cast<std::size_t>(count) * EntrySize);
    pak->m_file.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size()));
    if (!pak->m_file) {
        Log::GetEngineLogger().Error("The asset pak '{}' is cut short", PathToUtf8(path));
        return nullptr;
    }
    BinaryReader reader(table);
    for (std::uint32_t i = 0; i < count; ++i) {
        AssetId id;
        id.high = reader.Read<std::uint64_t>();
        id.low = reader.Read<std::uint64_t>();
        Entry entry{};
        entry.offset = reader.Read<std::uint64_t>();
        entry.storedSize = reader.Read<std::uint64_t>();
        entry.size = reader.Read<std::uint64_t>();
        entry.compressed = reader.Read<std::uint32_t>() != 0;
        pak->m_entries.emplace(id, entry);
    }
    if (!reader.IsValid()) {
        return nullptr;
    }
    return pak;
}

std::optional<std::vector<std::byte>> AssetPak::Read(AssetId id) const {
    const auto found = m_entries.find(id);
    if (found == m_entries.end()) {
        return std::nullopt;
    }
    const Entry& entry = found->second;
    std::vector<std::byte> stored(static_cast<std::size_t>(entry.storedSize));
    {
        const std::scoped_lock lock(m_mutex);
        m_file.clear();
        m_file.seekg(static_cast<std::streamoff>(entry.offset));
        m_file.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
        if (!m_file) {
            Log::GetEngineLogger().Error("Could not read asset {} from its pak", id.ToString());
            return std::nullopt;
        }
    }
    if (!entry.compressed) {
        return stored;
    }
    std::vector<std::byte> data(static_cast<std::size_t>(entry.size));
    const int size =
        LZ4_decompress_safe(reinterpret_cast<const char*>(stored.data()), reinterpret_cast<char*>(data.data()),
                            static_cast<int>(stored.size()), static_cast<int>(data.size()));
    if (size != static_cast<int>(data.size())) {
        Log::GetEngineLogger().Error("Asset {} in its pak is corrupt", id.ToString());
        return std::nullopt;
    }
    return data;
}

bool WriteAssetPak(const std::filesystem::path& path, const std::filesystem::path& importDirectory) {
    struct Item {
        AssetId id;
        std::vector<std::byte> stored;
        std::uint64_t size;
        bool compressed;
    };
    std::vector<Item> items;
    std::error_code error;
    for (const auto& file : std::filesystem::directory_iterator(importDirectory, error)) {
        if (!file.is_regular_file(error) || file.path().extension() != ".asset") {
            continue;
        }
        const auto id = AssetId::Parse(PathToUtf8(file.path().stem()));
        auto bytes = id ? ReadFileBytes(file.path()) : std::nullopt;
        if (!bytes) {
            continue;
        }
        if (bytes->size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 2)) {
            Log::GetEngineLogger().Error("Asset {} is too large for a pak", id->ToString());
            return false;
        }
        Item item{*id, {}, bytes->size(), false};
        std::vector<std::byte> compressed(static_cast<std::size_t>(LZ4_compressBound(static_cast<int>(bytes->size()))));
        const int size = LZ4_compress_default(reinterpret_cast<const char*>(bytes->data()),
                                              reinterpret_cast<char*>(compressed.data()),
                                              static_cast<int>(bytes->size()), static_cast<int>(compressed.size()));
        // Only kept compressed when that saves something worth decompressing for.
        if (size > 0 && static_cast<std::size_t>(size) < bytes->size() - bytes->size() / 16) {
            compressed.resize(static_cast<std::size_t>(size));
            item.stored = std::move(compressed);
            item.compressed = true;
        } else {
            item.stored = std::move(*bytes);
        }
        items.push_back(std::move(item));
    }
    if (error) {
        Log::GetEngineLogger().Error("Could not list the imported assets in '{}'", PathToUtf8(importDirectory));
        return false;
    }
    // In a stable order, so packing the same assets makes the same file.
    std::ranges::sort(items, {}, &Item::id);

    BinaryWriter header;
    header.Write(PakMagic);
    header.Write(PakVersion);
    header.Write(static_cast<std::uint32_t>(items.size()));
    std::uint64_t offset = 12 + items.size() * EntrySize;
    for (const Item& item : items) {
        header.Write(item.id.high);
        header.Write(item.id.low);
        header.Write(offset);
        header.Write(static_cast<std::uint64_t>(item.stored.size()));
        header.Write(item.size);
        header.Write(static_cast<std::uint32_t>(item.compressed));
        offset += item.stored.size();
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(header.GetData().data()),
               static_cast<std::streamsize>(header.GetData().size()));
    for (const Item& item : items) {
        file.write(reinterpret_cast<const char*>(item.stored.data()), static_cast<std::streamsize>(item.stored.size()));
    }
    if (!file) {
        Log::GetEngineLogger().Error("Could not write the asset pak '{}'", PathToUtf8(path));
        return false;
    }
    return true;
}

} // namespace Spot::Detail
