#pragma once

// Engine-internal: the binary files imported assets are stored in, under the import directory as "<id>.asset".
// Each starts with a header naming the format version and asset type, followed by the type's own data.

#include "assets/asset_id.h"
#include "assets/asset_type.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Spot::Detail {

// Bumped when the header changes; each type's data has its own version in its importer.
inline constexpr std::uint32_t AssetFileVersion = 1;

class BinaryWriter {
public:
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void Write(const T& value) {
        WriteBytes(std::as_bytes(std::span(&value, 1)));
    }

    void WriteBytes(std::span<const std::byte> bytes);
    // A 32-bit length, then the characters.
    void WriteString(std::string_view text);

    std::span<const std::byte> GetData() const { return m_data; }

private:
    std::vector<std::byte> m_data;
};

// Reads what a BinaryWriter wrote. Reading past the end fails the reader instead of reading out of bounds, so
// truncated or corrupt files are caught by checking IsValid() at the end.
class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::byte> data) : m_data(data) {}

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    T Read() {
        T value{};
        ReadBytes(std::as_writable_bytes(std::span(&value, 1)));
        return value;
    }

    void ReadBytes(std::span<std::byte> out);
    std::string ReadString();
    // A view of the next bytes, or an empty span when fewer remain.
    std::span<const std::byte> ReadView(std::size_t size);

    bool IsValid() const { return m_valid; }
    bool IsAtEnd() const { return m_offset == m_data.size(); }

private:
    std::span<const std::byte> m_data;
    std::size_t m_offset = 0;
    bool m_valid = true;
};

std::filesystem::path GetImportedAssetPath(const std::filesystem::path& importDirectory, AssetId id);

bool WriteAssetFile(const std::filesystem::path& path, AssetType type, std::span<const std::byte> data);
// The data after the header; nothing, after logging why, when the file is missing, of another version or type.
std::optional<std::vector<std::byte>> ReadAssetFile(const std::filesystem::path& path, AssetType type);

std::optional<std::vector<std::byte>> ReadFileBytes(const std::filesystem::path& path);

// 64-bit FNV-1a, to notice when sources and settings change.
class Hasher {
public:
    void Add(std::span<const std::byte> bytes);
    void Add(std::string_view text) { Add(std::as_bytes(std::span(text))); }
    std::uint64_t Get() const { return m_hash; }

private:
    std::uint64_t m_hash = 0xcbf29ce484222325ull;
};

} // namespace Spot::Detail
