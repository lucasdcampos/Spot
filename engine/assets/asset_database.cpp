#include "assets/asset_database.h"

#include "assets/asset_file.h"
#include "assets/asset_manifest.h"
#include "assets/importers/importer.h"
#include "core/log.h"
#include "core/paths.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace Spot {

namespace {

bool IsHidden(const std::filesystem::path& path) {
    const std::u8string name = path.filename().u8string();
    return !name.empty() && name.front() == u8'.';
}

} // namespace

AssetDatabase::AssetDatabase(std::filesystem::path sourceDirectory, std::filesystem::path importDirectory)
    : m_sourceDirectory(std::move(sourceDirectory)), m_importDirectory(std::move(importDirectory)) {
}

std::size_t AssetDatabase::Refresh() {
    // Without sources, importing would delete every imported asset, which may be all a game has.
    if (!Scan()) {
        m_assets.clear();
        m_assetIndices.clear();
        return 0;
    }
    return Import(nullptr);
}

bool AssetDatabase::Reimport(AssetId id) {
    if (!GetMeta(id)) {
        return false;
    }
    return Import(&id) == 1;
}

bool AssetDatabase::Scan() {
    m_sources.clear();
    m_sourceIndices.clear();

    std::error_code error;
    if (!std::filesystem::is_directory(m_sourceDirectory, error)) {
        Log::GetEngineLogger().Error("Asset source directory '{}' does not exist", PathToUtf8(m_sourceDirectory));
        return false;
    }

    std::vector<Source> found;
    for (auto it = std::filesystem::recursive_directory_iterator(m_sourceDirectory, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (it->is_directory() && IsHidden(it->path())) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && GetSourceAssetType(it->path())) {
            found.push_back(
                {it->path(), PathToUtf8(it->path().lexically_relative(m_sourceDirectory).generic_u8string()), {}});
        }
    }
    if (error) {
        Log::GetEngineLogger().Error("Could not read asset source directory '{}': {}", PathToUtf8(m_sourceDirectory),
                                     error.message());
    }
    // A stable order, so the copy of a file is the one that gets a new id whatever order the system lists them in.
    std::ranges::sort(found, {}, &Source::relativePath);

    std::unordered_set<AssetId> ids;
    for (auto& source : found) {
        const AssetType type = *GetSourceAssetType(source.path);
        const auto metaPath = AssetMeta::GetMetaPath(source.path);

        bool save = false;
        if (std::filesystem::exists(metaPath, error)) {
            if (auto meta = AssetMeta::LoadFromFile(metaPath)) {
                source.meta = std::move(*meta);
            } else {
                Log::GetEngineLogger().Warning("Asset '{}' gets a new id, since its meta file is unreadable",
                                               source.relativePath);
                source.meta.id = AssetId::Generate();
                save = true;
            }
        } else {
            source.meta.id = AssetId::Generate();
            save = true;
        }

        if (source.meta.type != type) {
            source.meta.type = type;
            save = true;
        }
        if (ids.contains(source.meta.id)) {
            Log::GetEngineLogger().Warning("Asset '{}' has the id of another asset, as copies of a file with its meta "
                                           "file do; it gets a new id",
                                           source.relativePath);
            source.meta.id = AssetId::Generate();
            source.meta.subAssets.clear();
            save = true;
        }
        ids.insert(source.meta.id);
        for (const auto& subAsset : source.meta.subAssets) {
            ids.insert(subAsset.id);
        }

        if (save) {
            source.meta.SaveToFile(metaPath);
        }
    }

    m_sources = std::move(found);
    for (std::size_t i = 0; i < m_sources.size(); ++i) {
        m_sourceIndices[m_sources[i].meta.id] = i;
    }
    return true;
}

std::size_t AssetDatabase::Import(const AssetId* forced) {
    std::error_code error;
    std::filesystem::create_directories(m_importDirectory, error);
    if (error) {
        Log::GetEngineLogger().Error("Could not create the asset import directory '{}': {}",
                                     PathToUtf8(m_importDirectory), error.message());
    }

    const auto manifestPath = Detail::Manifest::GetPath(m_importDirectory);
    std::unordered_map<AssetId, std::vector<Detail::ManifestEntry>> previous;
    if (auto manifest = Detail::Manifest::LoadFromFile(manifestPath)) {
        for (auto& entry : manifest->entries) {
            const AssetId source = entry.parent.IsValid() ? entry.parent : entry.id;
            previous[source].push_back(std::move(entry));
        }
    }

    Detail::Manifest manifest;
    std::size_t importedCount = 0;
    for (auto& source : m_sources) {
        const Detail::ImporterInfo importer = Detail::FindImporter(source.meta.type);
        Detail::ManifestEntry sourceEntry;
        sourceEntry.id = source.meta.id;
        sourceEntry.type = source.meta.type;
        sourceEntry.path = source.relativePath;
        if (!importer.import) {
            manifest.entries.push_back(std::move(sourceEntry));
            continue;
        }

        // Up to date when the last import succeeded with the same inputs and its files are still there.
        const auto found = previous.find(source.meta.id);
        const std::vector<Detail::ManifestEntry>* last = found != previous.end() ? &found->second : nullptr;
        const Detail::ManifestEntry* lastSource = last && !last->front().parent.IsValid() ? &last->front() : nullptr;
        if (lastSource && (!forced || *forced != source.meta.id) && lastSource->imported &&
            lastSource->hash == HashSource(source, lastSource->dependencies, importer.version) &&
            std::ranges::all_of(*last, [&](const Detail::ManifestEntry& entry) {
                return std::filesystem::exists(Detail::GetImportedAssetPath(m_importDirectory, entry.id), error);
            })) {
            for (auto entry : *last) {
                entry.path = source.relativePath;
                manifest.entries.push_back(std::move(entry));
            }
            continue;
        }

        const std::size_t knownSubAssets = source.meta.subAssets.size();
        Detail::ImportContext context(source.path, source.meta, m_importDirectory);
        const bool imported = importer.import(context);
        if (source.meta.subAssets.size() != knownSubAssets) {
            source.meta.SaveToFile(AssetMeta::GetMetaPath(source.path));
        }
        if (!imported) {
            Log::GetEngineLogger().Error("Could not import asset '{}'", source.relativePath);
            manifest.entries.push_back(std::move(sourceEntry));
            continue;
        }

        for (const auto& dependency : context.GetDependencies()) {
            sourceEntry.dependencies.push_back(
                PathToUtf8(dependency.lexically_relative(m_sourceDirectory).generic_u8string()));
        }
        sourceEntry.imported = true;
        sourceEntry.hash = HashSource(source, sourceEntry.dependencies, importer.version);
        manifest.entries.push_back(std::move(sourceEntry));
        for (const auto& asset : context.GetImportedAssets()) {
            if (asset.parent.IsValid()) {
                Detail::ManifestEntry entry;
                entry.id = asset.id;
                entry.type = asset.type;
                entry.path = source.relativePath;
                entry.name = asset.name;
                entry.parent = asset.parent;
                manifest.entries.push_back(std::move(entry));
            }
        }
        ++importedCount;
        Log::GetEngineLogger().Debug("Imported asset '{}'", source.relativePath);
    }

    // Files of assets that are gone, or that an import no longer produces, would only take space.
    std::unordered_set<AssetId> kept;
    for (const auto& entry : manifest.entries) {
        kept.insert(entry.id);
    }
    for (const auto& file : std::filesystem::directory_iterator(m_importDirectory, error)) {
        const auto name = file.path().filename();
        const auto id = AssetId::Parse(PathToUtf8(name.stem()));
        if (name.extension() == ".asset" && id && !kept.contains(*id)) {
            std::filesystem::remove(file.path(), error);
        }
    }

    manifest.SaveToFile(manifestPath);

    m_assets.clear();
    m_assetIndices.clear();
    for (const auto& entry : manifest.entries) {
        m_assetIndices[entry.id] = m_assets.size();
        m_assets.push_back({entry.id, entry.type, entry.path, entry.name, entry.parent});
    }
    return importedCount;
}

std::uint64_t AssetDatabase::HashSource(const Source& source, const std::vector<std::string>& dependencies,
                                        std::uint32_t importerVersion) const {
    Detail::Hasher hasher;
    const auto addFile = [&](const std::filesystem::path& path) {
        if (const auto bytes = Detail::ReadFileBytes(path)) {
            hasher.Add(*bytes);
        } else {
            hasher.Add("missing");
        }
    };

    addFile(source.path);
    for (const auto& dependency : dependencies) {
        hasher.Add(dependency);
        addFile(m_sourceDirectory / Utf8ToPath(dependency));
    }
    hasher.Add(GetAssetTypeName(source.meta.type));
    hasher.Add(source.meta.GetSettingsString());
    hasher.Add(std::to_string(importerVersion));
    return hasher.Get();
}

const AssetInfo* AssetDatabase::Find(AssetId id) const {
    const auto found = m_assetIndices.find(id);
    return found != m_assetIndices.end() ? &m_assets[found->second] : nullptr;
}

std::optional<AssetId> AssetDatabase::FindByPath(std::string_view path) const {
    for (const auto& source : m_sources) {
        if (source.relativePath == path) {
            return source.meta.id;
        }
    }
    return std::nullopt;
}

AssetMeta* AssetDatabase::GetMeta(AssetId id) {
    const auto found = m_sourceIndices.find(id);
    return found != m_sourceIndices.end() ? &m_sources[found->second].meta : nullptr;
}

const AssetMeta* AssetDatabase::GetMeta(AssetId id) const {
    const auto found = m_sourceIndices.find(id);
    return found != m_sourceIndices.end() ? &m_sources[found->second].meta : nullptr;
}

bool AssetDatabase::SaveMeta(AssetId id) const {
    const auto found = m_sourceIndices.find(id);
    if (found == m_sourceIndices.end()) {
        return false;
    }
    const Source& source = m_sources[found->second];
    return source.meta.SaveToFile(AssetMeta::GetMetaPath(source.path));
}

} // namespace Spot
