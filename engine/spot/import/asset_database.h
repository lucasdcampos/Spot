#pragma once

#include "spot/assets/asset_type.h"
#include "spot/core/ids/asset_id.h"
#include "spot/import/asset_meta.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Spot {

struct AssetInfo {
    AssetId id;
    AssetType type = AssetType::Texture;
    // The source file, relative to the source directory, with '/' separators. Sub-assets share their file's path.
    std::string path;
    // What a sub-asset is called within its file, such as a mesh's name; empty for the file's own asset.
    std::string name;
    // The asset of the file a sub-asset comes from; null for the file's own asset.
    AssetId parent;
};

// The source side of assets, as the editor and development builds see them: the files under a source directory,
// each with a .meta file beside it that gives it its id and import settings. Files are recognized by extension
// (see GetSourceAssetType()); folders whose name starts with '.' are skipped.
//
// Sources are imported into the engine's own binary formats in the import directory, along with a manifest of what
// it holds, which is all AssetManager needs: a game can ship the import directory without the sources. Imports are
// cached and only run again when a source, a file it depends on, its settings or its importer change.
class AssetDatabase {
public:
    AssetDatabase(std::filesystem::path sourceDirectory, std::filesystem::path importDirectory);

    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    const std::filesystem::path& GetSourceDirectory() const { return m_sourceDirectory; }
    const std::filesystem::path& GetImportDirectory() const { return m_importDirectory; }

    // Finds the source files again and imports the ones that changed. Files without a meta file get one with a new
    // id, and ids that clash, such as from a file copied with its meta file, are replaced in the copy. Imported
    // files of assets that are gone are deleted. Returns how many sources were imported.
    std::size_t Refresh();
    // Imports the source again, such as after its settings changed, even if nothing else did.
    bool Reimport(AssetId id);
    // The assets that the last Refresh() or Reimport() imported again or removed, sub-assets included, whose loaded
    // versions are out of date (see AssetManager::Reload()).
    std::span<const AssetId> GetChanged() const { return m_changed; }

    // Every asset found, in the order of their paths.
    std::span<const AssetInfo> GetAssets() const { return m_assets; }
    // Null for ids the database does not know.
    const AssetInfo* Find(AssetId id) const;
    // The asset of a source file, by its path relative to the source directory, such as "models/house.glb", or one
    // of its sub-assets, by the file's path and the sub-asset's name, such as "models/house.glb#Roof". When names
    // repeat within a file, the first one is found.
    std::optional<AssetId> FindByPath(std::string_view path) const;

    // The meta of a source file's asset; null for sub-assets and unknown ids. Changes to it are kept by SaveMeta(),
    // and used by the next import.
    AssetMeta* GetMeta(AssetId id);
    const AssetMeta* GetMeta(AssetId id) const;
    bool SaveMeta(AssetId id) const;

    // Changes to the source files and folders, by their paths relative to the source directory with '/' separators,
    // as tools make them. Meta files go along with their sources, so assets keep their ids. Nothing is imported
    // until the next Refresh(), which is meant to follow. Each returns false, after logging why, when it cannot be
    // done, such as for paths outside the source directory.

    // Creates a folder, with the folders above it.
    bool CreateFolder(std::string_view path);
    // Moves or renames a file with its meta file, or a folder with everything in it, to a path nothing uses yet.
    bool Move(std::string_view from, std::string_view to);
    // Deletes a file with its meta file, or a folder with everything in it.
    bool Delete(std::string_view path);
    // Copies a file to a path nothing uses yet, as a new asset with its own id and the same import settings.
    bool Duplicate(std::string_view from, std::string_view to);
    // Copies files and folders from elsewhere, such as dropped on an editor, into a folder of the sources, under
    // names nothing uses yet (see GetFreePath()). Returns the paths they got; files that could not be copied are
    // left out, after logging why.
    std::vector<std::string> CopyIn(std::span<const std::filesystem::path> files, std::string_view folder);
    // The path when nothing uses it, or else the first free one with a number after the name: "textures/crate 2.png"
    // for "textures/crate.png".
    std::string GetFreePath(std::string_view path) const;

    // Writes a material of a model to a new .material file at the path, relative to the source directory, and makes
    // the model use that file from then on, so the material can be edited. Imports both, and returns the new
    // material's id; nothing, after logging why, when the id is not a model's material or the file exists.
    std::optional<AssetId> ExtractMaterial(AssetId material, std::string_view path);

private:
    struct Source {
        std::filesystem::path path;
        std::string relativePath;
        AssetMeta meta;
    };

    // False when the source directory cannot be read.
    bool Scan();
    // Imports the sources that changed, or are forced to; returns how many were imported.
    std::size_t Import(const AssetId* forced);
    std::uint64_t HashSource(const Source& source, const std::vector<std::string>& dependencies,
                             std::uint32_t importerVersion) const;
    // As HashSource(), from the files' sizes and times of last change instead of their contents, which is much
    // faster to tell that nothing changed, along with the newest of those times.
    struct Stamp {
        std::uint64_t value = 0;
        std::filesystem::file_time_type newest;
    };
    Stamp StampSource(const Source& source, const std::vector<std::string>& dependencies,
                      std::uint32_t importerVersion) const;
    // The absolute path of a path relative to the source directory; nothing, after logging why, when it is empty or
    // outside the source directory.
    std::optional<std::filesystem::path> Resolve(std::string_view path) const;

    std::filesystem::path m_sourceDirectory;
    std::filesystem::path m_importDirectory;
    std::vector<Source> m_sources;
    std::vector<AssetInfo> m_assets;
    std::unordered_map<AssetId, std::size_t> m_assetIndices;
    std::unordered_map<AssetId, std::size_t> m_sourceIndices;
    std::vector<AssetId> m_changed;
};

} // namespace Spot
