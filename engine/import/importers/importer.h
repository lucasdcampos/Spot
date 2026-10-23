#pragma once

// Engine-internal: importers turn a source file into imported asset files. Each type of source has one; the asset
// database runs them when a source, its dependencies, its settings or the importer itself change.

#include "assets/asset_file.h"
#include "assets/asset_type.h"
#include "core/asset_id.h"
#include "import/asset_meta.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Spot::Detail {

struct ImportedAsset {
    AssetId id;
    AssetType type = AssetType::Texture;
    std::string name;
    // Null for the source file's own asset.
    AssetId parent;
};

class ImportContext {
public:
    ImportContext(std::filesystem::path sourcePath, AssetMeta& meta, std::filesystem::path importDirectory)
        : m_sourcePath(std::move(sourcePath)), m_meta(meta), m_importDirectory(std::move(importDirectory)) {}

    const std::filesystem::path& GetSourcePath() const { return m_sourcePath; }
    const AssetMeta& GetMeta() const { return m_meta; }

    // Writes the source file's own asset, under the meta's id.
    bool WriteAsset(AssetType type, const BinaryWriter& data);
    // Writes an asset made from part of the file, under the id the meta keeps for the key. Returns that id, or the
    // null id when writing failed.
    AssetId WriteSubAsset(std::string_view key, AssetType type, std::string name, const BinaryWriter& data);

    // Other files the import read, such as a .gltf's buffers, so changes to them import the source again.
    void AddDependency(const std::filesystem::path& path) { m_dependencies.push_back(path); }

    const std::vector<ImportedAsset>& GetImportedAssets() const { return m_imported; }
    const std::vector<std::filesystem::path>& GetDependencies() const { return m_dependencies; }

private:
    bool Write(AssetId id, AssetType type, const BinaryWriter& data);

    std::filesystem::path m_sourcePath;
    AssetMeta& m_meta;
    std::filesystem::path m_importDirectory;
    std::vector<ImportedAsset> m_imported;
    std::vector<std::filesystem::path> m_dependencies;
};

using Importer = bool (*)(ImportContext& context);

struct ImporterInfo {
    Importer import = nullptr;
    // Bumped whenever the importer's output changes, so every asset it made is imported again.
    std::uint32_t version = 0;
};

// Null import when the type has no importer.
ImporterInfo FindImporter(AssetType type);

bool ImportTexture(ImportContext& context);
bool ImportSpriteAtlas(ImportContext& context);
bool ImportModel(ImportContext& context);
bool ImportAnimatorController(ImportContext& context);
bool ImportMaterial(ImportContext& context);
bool ImportScene(ImportContext& context);
bool ImportScript(ImportContext& context);
bool ImportPrefab(ImportContext& context);
bool ImportSound(ImportContext& context);
bool ImportFont(ImportContext& context);

} // namespace Spot::Detail
