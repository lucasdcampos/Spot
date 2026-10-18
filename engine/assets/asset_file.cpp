#include "assets/asset_file.h"

#include "core/log.h"
#include "core/paths.h"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>

namespace Spot::Detail {

// Values are written in memory order, which is little-endian on every platform the engine targets, so files move
// between them unchanged.
static_assert(std::endian::native == std::endian::little, "Imported asset files assume a little-endian machine");

namespace {

constexpr std::array<char, 4> Magic{'S', 'P', 'O', 'T'};

} // namespace

void BinaryWriter::WriteBytes(std::span<const std::byte> bytes) {
    m_data.insert(m_data.end(), bytes.begin(), bytes.end());
}

void BinaryWriter::WriteString(std::string_view text) {
    Write(static_cast<std::uint32_t>(text.size()));
    WriteBytes(std::as_bytes(std::span(text)));
}

void BinaryReader::ReadBytes(std::span<std::byte> out) {
    const auto bytes = ReadView(out.size());
    if (bytes.size() == out.size()) {
        std::ranges::copy(bytes, out.begin());
    }
}

std::string BinaryReader::ReadString() {
    const auto size = Read<std::uint32_t>();
    const auto bytes = ReadView(size);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::span<const std::byte> BinaryReader::ReadView(std::size_t size) {
    if (!m_valid || size > m_data.size() - m_offset) {
        m_valid = false;
        return {};
    }
    const auto view = m_data.subspan(m_offset, size);
    m_offset += size;
    return view;
}

std::filesystem::path GetImportedAssetPath(const std::filesystem::path& importDirectory, AssetId id) {
    return importDirectory / (id.ToString() + ".asset");
}

bool WriteAssetFile(const std::filesystem::path& path, AssetType type, std::span<const std::byte> data) {
    BinaryWriter header;
    header.Write(Magic);
    header.Write(AssetFileVersion);
    header.Write(static_cast<std::uint32_t>(type));

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(header.GetData().data()),
               static_cast<std::streamsize>(header.GetData().size()));
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file) {
        Log::GetEngineLogger().Error("Could not write imported asset '{}'", PathToUtf8(path));
        return false;
    }
    return true;
}

std::optional<std::vector<std::byte>> ReadAssetFile(const std::filesystem::path& path, AssetType type) {
    auto bytes = ReadFileBytes(path);
    if (!bytes) {
        Log::GetEngineLogger().Error("Could not open imported asset '{}'", PathToUtf8(path));
        return std::nullopt;
    }
    return ParseAssetFile(std::move(*bytes), type, path);
}

std::optional<std::vector<std::byte>> ParseAssetFile(std::vector<std::byte> file, AssetType type,
                                                     const std::filesystem::path& path) {
    std::optional<std::vector<std::byte>> bytes(std::move(file));
    BinaryReader reader(*bytes);
    const auto magic = reader.Read<std::array<char, 4>>();
    const auto version = reader.Read<std::uint32_t>();
    const auto storedType = reader.Read<std::uint32_t>();
    if (!reader.IsValid() || magic != Magic) {
        Log::GetEngineLogger().Error("'{}' is not an imported asset", PathToUtf8(path));
        return std::nullopt;
    }
    if (version != AssetFileVersion) {
        Log::GetEngineLogger().Error("Imported asset '{}' has format version {}, not {}; import it again",
                                     PathToUtf8(path), version, AssetFileVersion);
        return std::nullopt;
    }
    if (storedType != static_cast<std::uint32_t>(type)) {
        Log::GetEngineLogger().Error("Imported asset '{}' is not a {}", PathToUtf8(path), GetAssetTypeName(type));
        return std::nullopt;
    }

    constexpr std::size_t HeaderSize = sizeof(Magic) + 2 * sizeof(std::uint32_t);
    bytes->erase(bytes->begin(), bytes->begin() + HeaderSize);
    return bytes;
}

void Hasher::Add(std::span<const std::byte> bytes) {
    for (const std::byte byte : bytes) {
        m_hash ^= static_cast<std::uint64_t>(byte);
        m_hash *= 0x100000001b3ull;
    }
}

} // namespace Spot::Detail
