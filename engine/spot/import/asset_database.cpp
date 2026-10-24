#include "spot/import/asset_database.h"

#include "spot/assets/asset_file.h"
#include "spot/assets/asset_formats.h"
#include "spot/assets/asset_manifest.h"
#include "spot/core/debug/log.h"
#include "spot/core/io/paths.h"
#include "spot/import/importers/importer.h"

#include <algorithm>
#include <chrono>
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

std::string ToRelative(const std::filesystem::path& path, const std::filesystem::path& root) {
    return PathToUtf8(path.lexically_relative(root).generic_u8string());
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

std::optional<AssetId> AssetDatabase::ExtractMaterial(AssetId material, std::string_view path) {
    const AssetInfo* info = Find(material);
    AssetMeta* model = info ? GetMeta(info->parent) : nullptr;
    const auto key =
        model ? std::ranges::find(model->subAssets, material, &SubAssetId::id) : std::vector<SubAssetId>::iterator();
    if (!info || info->type != AssetType::Material || !model || key == model->subAssets.end()) {
        Log::GetEngineLogger().Error("Only the materials of models can be extracted");
        return std::nullopt;
    }

    const auto target = m_sourceDirectory / Utf8ToPath(path);
    std::error_code error;
    if (GetSourceAssetType(target) != AssetType::Material || std::filesystem::exists(target, error)) {
        Log::GetEngineLogger().Error("Cannot extract a material to '{}': it must be a new .material file", path);
        return std::nullopt;
    }
    const auto data =
        Detail::ReadAssetFile(Detail::GetImportedAssetPath(m_importDirectory, material), AssetType::Material);
    const auto source = data ? Detail::DecodeMaterial(*data) : std::nullopt;
    if (!source) {
        Log::GetEngineLogger().Error("The imported material {} is missing or corrupt", material.ToString());
        return std::nullopt;
    }

    std::filesystem::create_directories(target.parent_path(), error);
    AssetMeta meta;
    meta.id = AssetId::Generate();
    meta.type = AssetType::Material;
    if (!source->SaveToFile(target) || !meta.SaveToFile(AssetMeta::GetMetaPath(target))) {
        return std::nullopt;
    }

    const std::string keyName = key->key;
    auto& overrides = model->model.materials;
    std::erase_if(overrides, [&](const MaterialOverride& entry) { return entry.key == keyName; });
    overrides.push_back({keyName, meta.id});
    SaveMeta(info->parent);

    Refresh();
    return meta.id;
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
    m_changed.clear();
    const std::int64_t now = std::filesystem::file_time_type::clock::now().time_since_epoch().count();
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
        // Files whose sizes and times did not change are taken as unchanged without reading them, unless they changed
        // so shortly before the last stamp that they could have changed again since within the same tick of the file
        // system's clock, keeping their time.
        Stamp stamp;
        const auto unchanged = [&] {
            stamp = StampSource(source, lastSource->dependencies, importer.version);
            const std::filesystem::file_time_type stampedAt{
                std::filesystem::file_time_type::duration(lastSource->stampedAt)};
            const bool settled = stamp.newest + std::chrono::seconds(2) < stampedAt;
            return (lastSource->stamp != 0 && lastSource->stamp == stamp.value && settled) ||
                   lastSource->hash == HashSource(source, lastSource->dependencies, importer.version);
        };
        if (lastSource && (!forced || *forced != source.meta.id) && lastSource->imported && unchanged() &&
            std::ranges::all_of(*last, [&](const Detail::ManifestEntry& entry) {
                return std::filesystem::exists(Detail::GetImportedAssetPath(m_importDirectory, entry.id), error);
            })) {
            for (auto entry : *last) {
                entry.path = source.relativePath;
                if (!entry.parent.IsValid()) {
                    entry.stamp = stamp.value;
                    entry.stampedAt = now;
                }
                manifest.entries.push_back(std::move(entry));
            }
            continue;
        }

        // What the source made before goes out of date, whether the import works or not.
        m_changed.push_back(source.meta.id);
        if (last) {
            for (const auto& entry : *last) {
                if (entry.parent.IsValid()) {
                    m_changed.push_back(entry.id);
                }
            }
        }
        const std::size_t knownSubAssets = source.meta.subAssets.size();
        Detail::ImportContext context(source.path, source.meta, m_importDirectory);
        context.SetSourceFinder([this](AssetId id) {
            const auto found = m_sourceIndices.find(id);
            return found != m_sourceIndices.end() ? m_sources[found->second].path : std::filesystem::path();
        });
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
        sourceEntry.stamp = StampSource(source, sourceEntry.dependencies, importer.version).value;
        sourceEntry.stampedAt = now;
        manifest.entries.push_back(std::move(sourceEntry));
        for (const auto& asset : context.GetImportedAssets()) {
            if (asset.parent.IsValid()) {
                m_changed.push_back(asset.id);
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
    for (const auto& [source, entries] : previous) {
        for (const auto& entry : entries) {
            if (!kept.contains(entry.id)) {
                m_changed.push_back(entry.id);
            }
        }
    }
    std::ranges::sort(m_changed);
    const auto [first, last] = std::ranges::unique(m_changed);
    m_changed.erase(first, last);
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

AssetDatabase::Stamp AssetDatabase::StampSource(const Source& source, const std::vector<std::string>& dependencies,
                                                std::uint32_t importerVersion) const {
    Detail::Hasher hasher;
    Stamp stamp;
    const auto addFile = [&](const std::filesystem::path& path) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        const auto time = error ? std::filesystem::file_time_type() : std::filesystem::last_write_time(path, error);
        hasher.Add(error ? std::string("missing")
                         : std::to_string(size) + ":" + std::to_string(time.time_since_epoch().count()));
        stamp.newest = std::max(stamp.newest, time);
    };

    addFile(source.path);
    for (const auto& dependency : dependencies) {
        hasher.Add(dependency);
        addFile(m_sourceDirectory / Utf8ToPath(dependency));
    }
    hasher.Add(GetAssetTypeName(source.meta.type));
    hasher.Add(source.meta.GetSettingsString());
    hasher.Add(std::to_string(importerVersion));
    stamp.value = hasher.Get();
    return stamp;
}

std::optional<std::filesystem::path> AssetDatabase::Resolve(std::string_view path) const {
    const std::filesystem::path relative = Utf8ToPath(path).lexically_normal();
    const auto first = relative.begin();
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        (first != relative.end() && (*first == ".." || *first == "."))) {
        Log::GetEngineLogger().Error("'{}' is not a path inside the asset sources", path);
        return std::nullopt;
    }
    return m_sourceDirectory / relative;
}

bool AssetDatabase::CreateFolder(std::string_view path) {
    const auto folder = Resolve(path);
    if (!folder) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(*folder, error);
    if (error || !std::filesystem::is_directory(*folder, error)) {
        Log::GetEngineLogger().Error("Could not create the folder '{}': {}", path, error.message());
        return false;
    }
    return true;
}

bool AssetDatabase::Move(std::string_view from, std::string_view to) {
    const auto source = Resolve(from);
    const auto target = Resolve(to);
    if (!source || !target) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(*source, error)) {
        Log::GetEngineLogger().Error("Cannot move '{}': it does not exist", from);
        return false;
    }
    // Changing only the case of a name finds the source itself on file systems that ignore case.
    if (std::filesystem::exists(*target, error) && !std::filesystem::equivalent(*source, *target, error)) {
        Log::GetEngineLogger().Error("Cannot move '{}' to '{}': something is there already", from, to);
        return false;
    }
    const auto [end, rest] = std::ranges::mismatch(*source, *target);
    if (end == source->end() && rest != target->end()) {
        Log::GetEngineLogger().Error("Cannot move the folder '{}' into itself", from);
        return false;
    }

    std::filesystem::create_directories(target->parent_path(), error);
    std::filesystem::rename(*source, *target, error);
    if (error) {
        Log::GetEngineLogger().Error("Could not move '{}' to '{}': {}", from, to, error.message());
        return false;
    }
    const auto meta = AssetMeta::GetMetaPath(*source);
    if (std::filesystem::is_regular_file(meta, error)) {
        std::filesystem::rename(meta, AssetMeta::GetMetaPath(*target), error);
        if (error) {
            Log::GetEngineLogger().Error("Could not move the meta file of '{}': {}", from, error.message());
            return false;
        }
    }
    return true;
}

bool AssetDatabase::Delete(std::string_view path) {
    const auto target = Resolve(path);
    if (!target) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(*target, error)) {
        Log::GetEngineLogger().Error("Cannot delete '{}': it does not exist", path);
        return false;
    }
    std::filesystem::remove_all(*target, error);
    if (!error) {
        std::filesystem::remove(AssetMeta::GetMetaPath(*target), error);
    }
    if (error) {
        Log::GetEngineLogger().Error("Could not delete '{}': {}", path, error.message());
        return false;
    }
    return true;
}

bool AssetDatabase::Duplicate(std::string_view from, std::string_view to) {
    const auto source = Resolve(from);
    const auto target = Resolve(to);
    if (!source || !target) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(*source, error) || std::filesystem::exists(*target, error)) {
        Log::GetEngineLogger().Error("Cannot duplicate '{}' to '{}': only files go to a path nothing uses", from, to);
        return false;
    }
    std::filesystem::create_directories(target->parent_path(), error);
    std::filesystem::copy_file(*source, *target, error);
    if (error) {
        Log::GetEngineLogger().Error("Could not duplicate '{}': {}", from, error.message());
        return false;
    }
    // The copy is imported the same way, as an asset of its own.
    if (auto meta = AssetMeta::LoadFromFile(AssetMeta::GetMetaPath(*source))) {
        meta->id = AssetId::Generate();
        meta->subAssets.clear();
        return meta->SaveToFile(AssetMeta::GetMetaPath(*target));
    }
    return true;
}

std::vector<std::string> AssetDatabase::CopyIn(std::span<const std::filesystem::path> files, std::string_view folder) {
    std::vector<std::string> copied;
    const auto destination = folder.empty() ? std::optional(m_sourceDirectory) : Resolve(folder);
    if (!destination) {
        return copied;
    }
    std::error_code error;
    std::filesystem::create_directories(*destination, error);
    for (const auto& file : files) {
        const std::string name = PathToUtf8(file.filename());
        const std::string path = GetFreePath(folder.empty() ? name : std::string(folder) + "/" + name);
        const auto target = m_sourceDirectory / Utf8ToPath(path);
        std::filesystem::copy(file, target,
                              std::filesystem::copy_options::recursive | std::filesystem::copy_options::copy_symlinks,
                              error);
        if (error) {
            Log::GetEngineLogger().Error("Could not copy '{}' into the assets: {}", PathToUtf8(file), error.message());
            error.clear();
            continue;
        }
        copied.push_back(path);
    }
    return copied;
}

std::string AssetDatabase::GetFreePath(std::string_view path) const {
    const std::filesystem::path wanted = Utf8ToPath(path);
    std::error_code error;
    if (!std::filesystem::exists(m_sourceDirectory / wanted, error)) {
        return std::string(path);
    }
    const std::filesystem::path folder = wanted.parent_path();
    const std::string stem = PathToUtf8(wanted.stem());
    const std::string extension = PathToUtf8(wanted.extension());
    for (int number = 2;; ++number) {
        const std::filesystem::path candidate = folder / Utf8ToPath(stem + " " + std::to_string(number) + extension);
        if (!std::filesystem::exists(m_sourceDirectory / candidate, error)) {
            return ToRelative(m_sourceDirectory / candidate, m_sourceDirectory);
        }
    }
}

const AssetInfo* AssetDatabase::Find(AssetId id) const {
    const auto found = m_assetIndices.find(id);
    return found != m_assetIndices.end() ? &m_assets[found->second] : nullptr;
}

std::optional<AssetId> AssetDatabase::FindByPath(std::string_view path) const {
    const auto separator = path.find('#');
    const std::string_view file = path.substr(0, separator);
    const std::string_view name = separator == std::string_view::npos ? std::string_view() : path.substr(separator + 1);
    if (separator != std::string_view::npos && name.empty()) {
        return std::nullopt;
    }
    for (const auto& asset : m_assets) {
        if (asset.path == file && asset.name == name && asset.parent.IsValid() == !name.empty()) {
            return asset.id;
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
