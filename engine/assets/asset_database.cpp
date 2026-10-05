#include "assets/asset_database.h"

#include "core/log.h"
#include "core/paths.h"

#include <algorithm>
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

void AssetDatabase::Refresh() {
    m_sources.clear();

    std::error_code error;
    if (!std::filesystem::is_directory(m_sourceDirectory, error)) {
        Log::GetEngineLogger().Error("Asset source directory '{}' does not exist", PathToUtf8(m_sourceDirectory));
        RebuildIndex();
        return;
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
    RebuildIndex();
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

void AssetDatabase::RebuildIndex() {
    m_assets.clear();
    m_assetIndices.clear();
    m_sourceIndices.clear();
    for (std::size_t i = 0; i < m_sources.size(); ++i) {
        const Source& source = m_sources[i];
        m_sourceIndices[source.meta.id] = i;
        m_assetIndices[source.meta.id] = m_assets.size();
        m_assets.push_back({source.meta.id, source.meta.type, source.relativePath, {}, {}});
    }
}

} // namespace Spot
