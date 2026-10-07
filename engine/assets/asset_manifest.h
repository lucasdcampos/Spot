#pragma once

// Engine-internal: the manifest.json file in the import directory, which lists every imported asset. The asset
// database writes it, and the asset manager reads it, so a game can ship with the import directory alone.

#include "assets/asset_id.h"
#include "assets/asset_type.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Spot::Detail {

struct ManifestEntry {
    AssetId id;
    AssetType type = AssetType::Texture;
    std::string path;
    std::string name;
    AssetId parent;

    // The rest is only for the source files' own assets.
    bool imported = false;
    // Of the source, its dependencies, its settings and its importer's version, as of the last import.
    std::uint64_t hash = 0;
    // The same, from the files' sizes and times of last change instead of their contents; 0 when unknown.
    std::uint64_t stamp = 0;
    // Relative to the source directory, with '/' separators.
    std::vector<std::string> dependencies;
};

struct Manifest {
    // Sources in order of their paths, each followed by its sub-assets.
    std::vector<ManifestEntry> entries;

    static std::filesystem::path GetPath(const std::filesystem::path& importDirectory);

    bool SaveToFile(const std::filesystem::path& path) const;
    // Nothing when the file is missing or unreadable.
    static std::optional<Manifest> LoadFromFile(const std::filesystem::path& path);
};

} // namespace Spot::Detail
