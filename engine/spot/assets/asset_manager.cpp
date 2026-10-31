#include "spot/assets/asset_manager.h"

#include "spot/assets/asset_file.h"
#include "spot/assets/asset_formats.h"
#include "spot/assets/asset_manifest.h"
#include "spot/assets/asset_pak.h"
#include "spot/assets/built_in_assets.h"
#include "spot/assets/primitive_meshes.h"
#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"
#include "spot/core/io/paths.h"
#include "spot/core/jobs/job_system.h"

#include <algorithm>
#include <array>
#include <exception>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Spot {

// An asset read and decoded as far as it goes without the GPU or other assets, which can be done on any thread.
struct Detail::PreparedAsset {
    AssetId id;
    AssetType type = AssetType::Texture;
    std::optional<std::vector<std::byte>> data = std::nullopt;
    AssetLoader loader = {};
    std::shared_ptr<void> decoded = {};
};

namespace {

struct BuiltIn {
    AssetId id;
    AssetType type;
    const char* path;
};

constexpr std::array BuiltIns{
    BuiltIn{BuiltInAssets::CubeMesh, AssetType::Mesh, "builtin/cube"},
    BuiltIn{BuiltInAssets::SphereMesh, AssetType::Mesh, "builtin/sphere"},
    BuiltIn{BuiltInAssets::PlaneMesh, AssetType::Mesh, "builtin/plane"},
    BuiltIn{BuiltInAssets::GridTexture, AssetType::Texture, "builtin/grid"},
    BuiltIn{BuiltInAssets::CheckerTexture, AssetType::Texture, "builtin/checker"},
};

} // namespace

AssetManager::AssetManager(std::filesystem::path importDirectory) : m_importDirectory(std::move(importDirectory)) {
    RefreshManifest();
}

AssetManager::~AssetManager() {
    // Preloads and loads still being read refer to the manager.
    for (const auto& task : m_preloads) {
        while (!task->ready.load()) {
            std::this_thread::yield();
        }
    }
    while (!m_pending.empty()) {
        if (!m_pending.front()->prepared.load()) {
            std::this_thread::yield();
            continue;
        }
        m_pending.pop_front();
    }
}

void AssetManager::RefreshManifest() {
    // Shipped games hold their assets in one pak; tools and the editor read the imported files themselves.
    const auto pakPath = m_importDirectory / Detail::AssetPakFileName;
    std::error_code error;
    m_pak = std::filesystem::is_regular_file(pakPath, error) ? Detail::AssetPak::Open(pakPath) : nullptr;
    const auto manifest = Detail::Manifest::LoadFromFile(Detail::Manifest::GetPath(m_importDirectory));
    if (!manifest) {
        // Applications without assets have no import folder at all, which is no cause for concern.
        const LogLevel level = std::filesystem::exists(m_importDirectory) ? LogLevel::Warning : LogLevel::Debug;
        Log::GetEngineLogger().Write(level, "No asset manifest in '{}'; no assets can be loaded",
                                     PathToUtf8(m_importDirectory));
    }

    std::unordered_map<AssetId, Entry> entries;
    m_paths.clear();
    for (const auto& entry : manifest ? manifest->entries : std::vector<Detail::ManifestEntry>()) {
        // Assets keep what they already loaded, so references to them stay shared.
        const auto previous = m_entries.find(entry.id);
        // Script diagnostics carry the source path, so a rename needs a new source object.
        const bool keepLoaded =
            previous != m_entries.end() && (entry.type != AssetType::Script || previous->second.path == entry.path);
        entries[entry.id] = {entry.type, entry.path,
                             keepLoaded ? previous->second.loaded : std::weak_ptr<const void>()};
        if (previous != m_entries.end()) {
            entries[entry.id].dependencies = previous->second.dependencies;
        }
        // The first of sub-assets with the same name keeps the path.
        m_paths.try_emplace(entry.parent.IsValid() ? entry.path + "#" + entry.name : entry.path, entry.id);
    }

    for (const auto& [id, type, path] : BuiltIns) {
        const auto previous = m_entries.find(id);
        entries[id] = {type, path, previous != m_entries.end() ? previous->second.loaded : std::weak_ptr<const void>()};
        m_paths[path] = id;
    }
    m_entries = std::move(entries);
}

std::optional<AssetType> AssetManager::GetType(AssetId id) const {
    const auto found = m_entries.find(id);
    return found != m_entries.end() ? std::optional(found->second.type) : std::nullopt;
}

std::optional<AssetId> AssetManager::FindByPath(std::string_view path) const {
    const auto found = m_paths.find(std::string(path));
    return found != m_paths.end() ? std::optional(found->second) : std::nullopt;
}

std::vector<AssetId> AssetManager::GetAssetIds(AssetType type, std::string_view directory) const {
    std::string prefix(directory);
    std::ranges::replace(prefix, '\\', '/');
    while (prefix.ends_with('/')) {
        prefix.pop_back();
    }
    if (!prefix.empty()) {
        prefix += '/';
    }
    std::vector<AssetId> ids;
    for (const auto& [id, entry] : m_entries) {
        const bool builtIn = std::ranges::any_of(BuiltIns, [&](const BuiltIn& asset) { return asset.id == id; });
        if (entry.type == type && entry.path.starts_with(prefix) && !builtIn) {
            ids.push_back(id);
        }
    }
    std::ranges::sort(ids, [&](AssetId a, AssetId b) {
        const auto& first = m_entries.at(a).path;
        const auto& second = m_entries.at(b).path;
        return first != second ? first < second : a.ToString() < b.ToString();
    });
    return ids;
}

void AssetManager::Forget(AssetId id) {
    if (const auto found = m_entries.find(id); found != m_entries.end()) {
        found->second.loaded.reset();
        found->second.meshData.reset();
    }
}

std::vector<AssetId> AssetManager::GetDependents(std::span<const AssetId> assets) const {
    std::vector<AssetId> dependents;
    for (const auto& [id, entry] : m_entries) {
        if (std::ranges::any_of(entry.dependencies, [&](AssetId dependency) {
                return std::ranges::find(assets, dependency) != assets.end();
            })) {
            dependents.push_back(id);
        }
    }
    return dependents;
}

void AssetManager::RegisterLoader(AssetType type, AssetLoader loader) {
    m_loaders[type] = std::move(loader);
}

std::optional<std::vector<std::byte>> AssetManager::ReadImportedData(AssetId id, AssetType type) const {
    if (GetType(id) != type) {
        Log::GetEngineLogger().Error("Asset {} is not a {}", id.ToString(), GetAssetTypeName(type));
        return std::nullopt;
    }
    return ReadData(id, type);
}

std::optional<std::vector<std::byte>> AssetManager::ReadData(AssetId id, AssetType type) const {
    if (m_pak && m_pak->Contains(id)) {
        auto file = m_pak->Read(id);
        return file
                   ? Detail::ParseAssetFile(std::move(*file), type, Detail::GetImportedAssetPath(m_importDirectory, id))
                   : std::nullopt;
    }
    return Detail::ReadAssetFile(Detail::GetImportedAssetPath(m_importDirectory, id), type);
}

std::string_view AssetManager::GetPath(AssetId id) const {
    const auto found = m_entries.find(id);
    return found != m_entries.end() ? std::string_view(found->second.path) : std::string_view();
}

void AssetManager::LogUnknownPath(std::string_view path) {
    Log::GetEngineLogger().Error("No asset at '{}'", path);
}

std::shared_ptr<const void> AssetManager::LoadAsset(AssetId id, AssetType type) {
    if (!id.IsValid()) {
        return nullptr;
    }
    const auto found = m_entries.find(id);
    if (found == m_entries.end()) {
        Log::GetEngineLogger().Error("No asset has the id {}", id.ToString());
        return nullptr;
    }
    if (found->second.type != type) {
        Log::GetEngineLogger().Error("Asset {} ('{}') is a {}, not a {}", id.ToString(), found->second.path,
                                     GetAssetTypeName(found->second.type), GetAssetTypeName(type));
        return nullptr;
    }
    if (auto loaded = found->second.loaded.lock()) {
        return loaded;
    }

    return Keep(id, type, Create(id, type));
}

std::shared_ptr<const void> AssetManager::Keep(AssetId id, AssetType type, std::shared_ptr<const void> asset) {
    if (!asset) {
        if (const auto loader = m_loaders.find(type); loader != m_loaders.end() && loader->second.unavailable) {
            return nullptr;
        }
        Log::GetEngineLogger().Error("Could not load {} {} from '{}'", GetAssetTypeName(type), id.ToString(),
                                     GetPath(id));
        return nullptr;
    }
    // Looked up again: loading the asset's own references may have added entries to the map.
    m_entries.at(id).loaded = asset;
    if (const auto loader = m_loaders.find(type); loader != m_loaders.end() && loader->second.dependencies) {
        m_entries.at(id).dependencies = loader->second.dependencies(asset.get());
    }
    return asset;
}

bool AssetManager::IsLoaded(AssetId id) const {
    const auto found = m_entries.find(id);
    return found != m_entries.end() && !found->second.loaded.expired();
}

std::unordered_map<AssetId, AssetType> AssetManager::GetUnloadedTypes() const {
    std::unordered_map<AssetId, AssetType> types;
    types.reserve(m_entries.size());
    for (const auto& [id, entry] : m_entries) {
        if (entry.loaded.expired() && !IsBuiltIn(id)) {
            types.emplace(id, entry.type);
        }
    }
    return types;
}

std::vector<std::vector<Detail::PreparedAsset>>
AssetManager::PrepareWaves(std::span<const AssetId> ids, const std::unordered_map<AssetId, AssetType>& types) const {
    // Wave by wave: the assets of a wave are read and decoded at once, on the job system's threads, which tells the
    // assets they refer to, the next wave. Only what the types name is looked at, so this may run on another thread.
    const auto known = [&types](AssetId id) { return types.contains(id); };
    std::unordered_set<AssetId> seen;
    std::vector<AssetId> wave;
    for (const AssetId id : ids) {
        if (known(id) && seen.insert(id).second) {
            wave.push_back(id);
        }
    }
    std::vector<std::vector<Detail::PreparedAsset>> waves;
    while (!wave.empty()) {
        std::vector<Detail::PreparedAsset> prepared(wave.size());
        JobSystem::GetShared().ParallelFor(wave.size(), 1, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                prepared[i] = Prepare(wave[i], types.at(wave[i]));
            }
        });
        std::vector<AssetId> next;
        const auto refer = [&](AssetId id) {
            if (id.IsValid() && known(id) && seen.insert(id).second) {
                next.push_back(id);
            }
        };
        for (const Detail::PreparedAsset& asset : prepared) {
            ForEachReference(asset, known, refer);
        }
        waves.push_back(std::move(prepared));
        wave = std::move(next);
    }
    return waves;
}

std::vector<std::shared_ptr<const void>>
AssetManager::FinishWaves(std::vector<std::vector<Detail::PreparedAsset>>& waves) {
    // Made from the last wave back, so each finds what it refers to loaded already.
    std::vector<std::shared_ptr<const void>> loaded;
    for (auto it = waves.rbegin(); it != waves.rend(); ++it) {
        for (Detail::PreparedAsset& asset : *it) {
            // Scenes are not shared assets: what they refer to is all they are preloaded for. Assets loaded since
            // they were read are kept as they are.
            if (asset.type == AssetType::Scene || !asset.data || IsLoaded(asset.id) || !m_entries.contains(asset.id)) {
                continue;
            }
            const AssetId id = asset.id;
            const AssetType type = asset.type;
            if (auto kept = Keep(id, type, Finish(std::move(asset)))) {
                loaded.push_back(std::move(kept));
            }
        }
    }
    waves.clear();
    return loaded;
}

std::vector<std::shared_ptr<const void>> AssetManager::Preload(std::span<const AssetId> ids) {
    SPOT_PROFILE_SCOPE("AssetManager::Preload");
    auto waves = PrepareWaves(ids, GetUnloadedTypes());
    return FinishWaves(waves);
}

std::shared_ptr<Detail::PreloadTask> AssetManager::StartPreload(std::span<const AssetId> ids) {
    auto task = std::make_shared<Detail::PreloadTask>();
    task->ids.assign(ids.begin(), ids.end());
    // Taken here, so the job never reads what this thread changes.
    task->types = std::make_shared<std::unordered_map<AssetId, AssetType>>(GetUnloadedTypes());
    std::erase_if(m_preloads, [](const auto& running) { return running->ready.load(); });
    m_preloads.push_back(task);
    JobSystem::GetShared().Run([this, task] {
        try {
            auto waves = PrepareWaves(task->ids, *task->types);
            task->waves = std::make_shared<std::vector<std::vector<Detail::PreparedAsset>>>(std::move(waves));
        } catch (const std::exception& error) {
            Log::GetEngineLogger().Error("Asset preload failed: {}", error.what());
        } catch (...) {
            Log::GetEngineLogger().Error("Asset preload failed");
        }
        task->ready = true;
    });
    return task;
}

std::vector<std::shared_ptr<const void>> AssetManager::FinishPreload(Detail::PreloadTask& task) {
    while (!task.ready.load()) {
        std::this_thread::yield();
    }
    if (!task.waves) {
        return {};
    }
    auto& waves = *std::static_pointer_cast<std::vector<std::vector<Detail::PreparedAsset>>>(task.waves);
    return FinishWaves(waves);
}

std::vector<std::shared_ptr<const void>> AssetManager::PreloadReferencedBy(std::string_view text) {
    std::vector<AssetId> ids;
    ForEachIdInText(
        std::as_bytes(std::span(text)), [this](AssetId id) { return m_entries.contains(id); },
        [&](AssetId id) { ids.push_back(id); });
    return Preload(ids);
}

void AssetManager::ForEachReference(const Detail::PreparedAsset& asset, const std::function<bool(AssetId)>& known,
                                    const std::function<void(AssetId)>& fn) const {
    if (asset.decoded && asset.loader.references) {
        for (const AssetId id : asset.loader.references(asset.decoded.get())) {
            fn(id);
        }
    }
    if (asset.data && (asset.type == AssetType::Scene || asset.loader.textReferences)) {
        ForEachIdInText(*asset.data, known, fn);
    }
}

void AssetManager::ForEachIdInText(std::span<const std::byte> text, const std::function<bool(AssetId)>& known,
                                   const std::function<void(AssetId)>& fn) {
    const auto isHex = [](std::byte b) {
        const auto c = static_cast<char>(b);
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    };
    std::size_t run = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && isHex(text[i])) {
            ++run;
            continue;
        }
        if (run == 32) {
            const std::string_view digits(reinterpret_cast<const char*>(text.data()) + i - 32, 32);
            if (const auto id = AssetId::Parse(digits); id && known(*id)) {
                fn(*id);
            }
        }
        run = 0;
    }
}

bool AssetManager::IsBuiltIn(AssetId id) {
    return std::ranges::any_of(BuiltIns, [id](const BuiltIn& builtIn) { return builtIn.id == id; });
}

void AssetManager::Update() {
    // Finished in the order they were asked for, so what loads first shows first.
    while (!m_pending.empty() && m_pending.front()->prepared.load()) {
        const std::shared_ptr<Detail::AsyncLoad> load = std::move(m_pending.front());
        m_pending.pop_front();
        if (const auto found = m_entries.find(load->id); found != m_entries.end()) {
            if (auto loaded = found->second.loaded.lock()) {
                load->result = std::move(loaded);
            } else {
                load->result = Keep(load->id, load->type,
                                    Finish(std::move(*std::static_pointer_cast<Detail::PreparedAsset>(load->asset))));
            }
        }
        load->asset.reset();
        load->done = true;
    }
}

std::shared_ptr<Detail::AsyncLoad> AssetManager::StartAsyncLoad(AssetId id, AssetType type) {
    auto load = std::make_shared<Detail::AsyncLoad>();
    load->id = id;
    load->type = type;
    const auto found = m_entries.find(id);
    if (found == m_entries.end() || found->second.type != type || IsBuiltIn(id)) {
        // Unknown, of another type, or built in: Load() says why, or makes it, at once.
        load->result = LoadAsset(id, type);
        load->done = true;
        return load;
    }
    if (auto loaded = found->second.loaded.lock()) {
        load->result = std::move(loaded);
        load->done = true;
        return load;
    }
    m_pending.push_back(load);
    // The job holds the load, not the manager, which may finish waiting for it.
    JobSystem::GetShared().Run([this, load] {
        load->asset = std::make_shared<Detail::PreparedAsset>(Prepare(load->id, load->type));
        load->prepared = true;
    });
    return load;
}

std::shared_ptr<const MeshData> AssetManager::LoadMeshData(AssetId id) {
    if (!id.IsValid()) {
        return nullptr;
    }
    const auto found = m_entries.find(id);
    if (found == m_entries.end() || found->second.type != AssetType::Mesh) {
        Log::GetEngineLogger().Error("No mesh asset has the id {}", id.ToString());
        return nullptr;
    }
    if (auto loaded = found->second.meshData.lock()) {
        return loaded;
    }
    std::shared_ptr<const MeshData> mesh;
    if (id == BuiltInAssets::CubeMesh) {
        mesh = std::make_shared<const MeshData>(PrimitiveMeshes::CreateCube());
    } else if (id == BuiltInAssets::SphereMesh) {
        mesh = std::make_shared<const MeshData>(PrimitiveMeshes::CreateSphere());
    } else if (id == BuiltInAssets::PlaneMesh) {
        mesh = std::make_shared<const MeshData>(PrimitiveMeshes::CreatePlane());
    } else if (const auto data = ReadData(id, AssetType::Mesh)) {
        if (auto decoded = Detail::DecodeMesh(*data)) {
            mesh = std::make_shared<const MeshData>(std::move(*decoded));
        }
    }
    if (!mesh) {
        Log::GetEngineLogger().Error("Could not load the triangles of mesh {} from '{}'", id.ToString(),
                                     found->second.path);
        return nullptr;
    }
    found->second.meshData = mesh;
    return mesh;
}

std::shared_ptr<const void> AssetManager::Create(AssetId id, AssetType type) {
    return Finish(Prepare(id, type));
}

Detail::PreparedAsset AssetManager::Prepare(AssetId id, AssetType type) const {
    Detail::PreparedAsset prepared{.id = id, .type = type};
    if (const auto loader = m_loaders.find(type); loader != m_loaders.end()) {
        prepared.loader = loader->second;
    }
    if (prepared.loader.unavailable) {
        return prepared;
    }
    prepared.data = IsBuiltIn(id) ? std::optional(std::vector<std::byte>()) : ReadData(id, type);
    if (prepared.data && prepared.loader.prepare) {
        try {
            prepared.decoded = prepared.loader.prepare(id, *prepared.data);
        } catch (const std::exception& error) {
            Log::GetEngineLogger().Error("Asset {} decoder failed: {}", id.ToString(), error.what());
        } catch (...) {
            Log::GetEngineLogger().Error("Asset {} decoder failed", id.ToString());
        }
    }
    return prepared;
}

std::shared_ptr<const void> AssetManager::Finish(Detail::PreparedAsset prepared) {
    if (prepared.decoded && prepared.loader.create) {
        try {
            return prepared.loader.create(*this, prepared.id, prepared.decoded.get());
        } catch (const std::exception& error) {
            Log::GetEngineLogger().Error("Asset {} creation failed: {}", prepared.id.ToString(), error.what());
        } catch (...) {
            Log::GetEngineLogger().Error("Asset {} creation failed", prepared.id.ToString());
        }
    }
    if (!prepared.loader.unavailable) {
        Log::GetEngineLogger().Error("Asset {} could not be decoded or has no registered loader",
                                     prepared.id.ToString());
    }
    return nullptr;
}

} // namespace Spot
