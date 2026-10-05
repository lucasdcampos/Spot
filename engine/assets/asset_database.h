#pragma once

#include "assets/asset_id.h"
#include "assets/asset_meta.h"
#include "assets/asset_type.h"

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
class AssetDatabase {
public:
    AssetDatabase(std::filesystem::path sourceDirectory, std::filesystem::path importDirectory);

    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    const std::filesystem::path& GetSourceDirectory() const { return m_sourceDirectory; }
    const std::filesystem::path& GetImportDirectory() const { return m_importDirectory; }

    // Finds the source files again. Files without a meta file get one with a new id, and ids that clash, such as
    // from a file copied with its meta file, are replaced in the copy.
    void Refresh();

    // Every asset found, in the order of their paths.
    std::span<const AssetInfo> GetAssets() const { return m_assets; }
    // Null for ids the database does not know.
    const AssetInfo* Find(AssetId id) const;
    // The asset of a source file, by its path relative to the source directory, such as "models/house.glb".
    std::optional<AssetId> FindByPath(std::string_view path) const;

    // The meta of a source file's asset; null for sub-assets and unknown ids. Changes to it are kept by SaveMeta().
    AssetMeta* GetMeta(AssetId id);
    const AssetMeta* GetMeta(AssetId id) const;
    bool SaveMeta(AssetId id) const;

private:
    struct Source {
        std::filesystem::path path;
        std::string relativePath;
        AssetMeta meta;
    };

    void RebuildIndex();

    std::filesystem::path m_sourceDirectory;
    std::filesystem::path m_importDirectory;
    std::vector<Source> m_sources;
    std::vector<AssetInfo> m_assets;
    std::unordered_map<AssetId, std::size_t> m_assetIndices;
    std::unordered_map<AssetId, std::size_t> m_sourceIndices;
};

} // namespace Spot
