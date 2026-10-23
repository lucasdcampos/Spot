#include "spot/assets/asset_manager.h"
#include "spot/graphics/sprite_atlas.h"

#include "spot/animation/animator_controller.h"
#include "spot/assets/asset_file.h"
#include "spot/assets/asset_formats.h"
#include "spot/assets/asset_manifest.h"
#include "spot/assets/asset_pak.h"
#include "spot/assets/built_in_assets.h"
#include "spot/assets/script_source.h"
#include "spot/audio/sound.h"
#include "spot/core/job_system.h"
#include "spot/core/log.h"
#include "spot/core/paths.h"
#include "spot/core/profiler.h"
#include "spot/graphics/font.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/image.h"
#include "spot/graphics/material.h"
#include "spot/graphics/mesh.h"
#include "spot/graphics/model.h"

#include <algorithm>
#include <array>
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
    std::optional<TextureFile> texture = std::nullopt;
    std::optional<MeshData> mesh = std::nullopt;
    std::optional<MaterialSource> material = std::nullopt;
    std::optional<std::vector<ModelFileNode>> model = std::nullopt;
    // Rasterizing a font's glyphs takes the longest of loading it.
    std::optional<FontAtlas> fontAtlas = std::nullopt;
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

AssetManager::AssetManager(std::filesystem::path importDirectory, GraphicsDevice* device)
    : m_importDirectory(std::move(importDirectory)), m_device(device) {
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

std::vector<AssetId> AssetManager::Reload(std::span<const AssetId> assets) {
    std::unordered_set<AssetId> stale(assets.begin(), assets.end());
    // Loaded materials and models hold on to other assets, so they go out of date with them, as do models with
    // their materials, until nothing more does.
    const auto refersToStale = [&](const Entry& entry, const std::shared_ptr<const void>& loaded) {
        if (entry.type == AssetType::SpriteAtlas) {
            return stale.contains(static_cast<const SpriteAtlas*>(loaded.get())->texture.GetId());
        }
        if (entry.type == AssetType::Material) {
            const auto& material = *static_cast<const Material*>(loaded.get());
            return std::ranges::any_of(
                std::array{&material.baseColorTexture, &material.metallicRoughnessTexture, &material.normalTexture,
                           &material.occlusionTexture, &material.emissiveTexture},
                [&](const AssetRef<Texture>* texture) { return stale.contains(texture->GetId()); });
        }
        if (entry.type == AssetType::AnimatorController) {
            return std::ranges::any_of(
                static_cast<const AnimatorController*>(loaded.get())->states, [&](const auto& state) {
                    return stale.contains(state.clip.GetId()) ||
                           std::ranges::any_of(state.blendTree.motions,
                                               [&](const auto& motion) { return stale.contains(motion.clip.GetId()); });
                });
        }
        if (entry.type == AssetType::Model) {
            const auto& model = *static_cast<const Model*>(loaded.get());
            return std::ranges::any_of(model.GetParts(),
                                       [&](const ModelPart& part) {
                                           return stale.contains(part.mesh.GetId()) ||
                                                  stale.contains(part.material.GetId());
                                       }) ||
                   std::ranges::any_of(model.GetNodes(), [&](const ModelNode& node) {
                       return stale.contains(node.skeleton.GetId()) ||
                              std::ranges::any_of(node.animations,
                                                  [&](const auto& clip) { return stale.contains(clip.GetId()); });
                   });
        }
        return false;
    };
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [id, entry] : m_entries) {
            const auto loaded = entry.loaded.lock();
            const bool prefabChanged =
                std::ranges::any_of(entry.dependencies, [&](AssetId dependency) { return stale.contains(dependency); });
            if (!stale.contains(id) && (prefabChanged || (loaded && refersToStale(entry, loaded)))) {
                stale.insert(id);
                grew = true;
            }
        }
    }
    std::vector<AssetId> reloaded;
    for (const AssetId id : stale) {
        if (const auto found = m_entries.find(id); found != m_entries.end()) {
            found->second.loaded.reset();
            found->second.meshData.reset();
        }
        reloaded.push_back(id);
    }
    std::ranges::sort(reloaded);
    return reloaded;
}

namespace {

std::unordered_map<AssetType, AssetLoader>& GetLoaders() {
    static std::unordered_map<AssetType, AssetLoader> loaders;
    return loaders;
}

} // namespace

void AssetManager::RegisterLoader(AssetType type, AssetLoader loader) {
    GetLoaders()[type] = loader;
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
        Log::GetEngineLogger().Error("Could not load {} {} from '{}'", GetAssetTypeName(type), id.ToString(),
                                     GetPath(id));
        return nullptr;
    }
    // Looked up again: loading the asset's own references may have added entries to the map.
    m_entries.at(id).loaded = asset;
    if (const auto loader = GetLoaders().find(type); loader != GetLoaders().end() && loader->second.dependencies) {
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
        auto waves = PrepareWaves(task->ids, *task->types);
        task->waves = std::make_shared<std::vector<std::vector<Detail::PreparedAsset>>>(std::move(waves));
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
    if (asset.material) {
        for (const AssetId texture :
             {asset.material->baseColorTexture, asset.material->metallicRoughnessTexture, asset.material->normalTexture,
              asset.material->occlusionTexture, asset.material->emissiveTexture}) {
            fn(texture);
        }
    }
    if (asset.model) {
        for (const auto& node : *asset.model) {
            fn(node.mesh);
            fn(node.skeleton);
            for (const AssetId id : node.materials) {
                fn(id);
            }
            for (const AssetId id : node.animations) {
                fn(id);
            }
        }
    }
    // Assets saved as text, such as scenes and prefabs, name the assets they refer to by their ids.
    if (asset.data && (asset.type == AssetType::Scene || asset.type == AssetType::Prefab ||
                       asset.type == AssetType::AnimatorController || asset.type == AssetType::SpriteAtlas)) {
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
        mesh = std::make_shared<const MeshData>(MeshData::CreateCube());
    } else if (id == BuiltInAssets::SphereMesh) {
        mesh = std::make_shared<const MeshData>(MeshData::CreateSphere());
    } else if (id == BuiltInAssets::PlaneMesh) {
        mesh = std::make_shared<const MeshData>(MeshData::CreatePlane());
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
    if (id == BuiltInAssets::CubeMesh || id == BuiltInAssets::SphereMesh || id == BuiltInAssets::PlaneMesh) {
        if (!m_device) {
            Log::GetEngineLogger().Error("Mesh assets need a graphics device");
            return nullptr;
        }
        const MeshData mesh = id == BuiltInAssets::CubeMesh     ? MeshData::CreateCube()
                              : id == BuiltInAssets::SphereMesh ? MeshData::CreateSphere()
                                                                : MeshData::CreatePlane();
        std::shared_ptr<const Mesh> result = Mesh::Create(*m_device, mesh);
        return result;
    }
    if (id == BuiltInAssets::GridTexture || id == BuiltInAssets::CheckerTexture) {
        if (!m_device) {
            Log::GetEngineLogger().Error("Texture assets need a graphics device");
            return nullptr;
        }
        const Image image = id == BuiltInAssets::GridTexture ? Image::CreateGrid() : Image::CreateChecker();
        std::shared_ptr<Texture> result = m_device->CreateTexture(image, true);
        return result;
    }

    return Finish(Prepare(id, type));
}

Detail::PreparedAsset AssetManager::Prepare(AssetId id, AssetType type) const {
    Detail::PreparedAsset prepared{.id = id, .type = type, .data = ReadData(id, type)};
    if (!prepared.data) {
        return prepared;
    }
    // What decodes without the GPU or other assets is done here, wherever this runs.
    switch (type) {
    case AssetType::Texture:
        prepared.texture = Detail::DecodeTexture(*prepared.data);
        break;
    case AssetType::Mesh:
        prepared.mesh = Detail::DecodeMesh(*prepared.data);
        break;
    case AssetType::Material:
        prepared.material = Detail::DecodeMaterial(*prepared.data);
        break;
    case AssetType::Model:
        prepared.model = Detail::DecodeModel(*prepared.data);
        break;
    case AssetType::Font:
        if (const auto font = Detail::DecodeFont(*prepared.data)) {
            prepared.fontAtlas = FontAtlas::Create(font->fontData, font->pixelSize);
        }
        break;
    default:
        break;
    }
    return prepared;
}

std::shared_ptr<const void> AssetManager::Finish(Detail::PreparedAsset prepared) {
    const AssetId id = prepared.id;
    const AssetType type = prepared.type;
    const auto& data = prepared.data;
    if (!data) {
        return nullptr;
    }

    const bool needsDevice = type == AssetType::Texture || type == AssetType::Mesh || type == AssetType::Font;
    if (needsDevice && !m_device) {
        Log::GetEngineLogger().Error("{} assets need a graphics device", GetAssetTypeName(type));
        return nullptr;
    }

    if (const auto loader = GetLoaders().find(type); loader != GetLoaders().end() && loader->second.load) {
        return loader->second.load(*this, id, *data);
    }

    switch (type) {
    case AssetType::Texture: {
        const auto& texture = prepared.texture;
        if (!texture) {
            break;
        }
        const TextureFormat pixelFormat = texture->srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8;
        if (texture->compressedFormat) {
            const TextureFormat format = *texture->compressedFormat;
            if (m_device->SupportsSampling(format)) {
                std::shared_ptr<Texture> result = m_device->CreateTexture({.width = texture->width,
                                                                           .height = texture->height,
                                                                           .format = format,
                                                                           .mipmaps = texture->levels.size() > 1});
                if (!result) {
                    break;
                }
                for (std::uint32_t i = 0; i < texture->levels.size() && i < result->GetMipLevelCount(); ++i) {
                    m_device->UploadToTextureLevel(*result, i, texture->levels[i]);
                }
                return result;
            }
            // The GPU cannot sample the blocks, so they are turned back into pixels here.
            Image image(texture->width, texture->height,
                        Detail::DecompressBlocks(format, texture->width, texture->height, texture->levels[0]));
            std::shared_ptr<Texture> result = m_device->CreateTexture(image, texture->mipmaps, pixelFormat);
            return result;
        }
        std::shared_ptr<Texture> result = m_device->CreateTexture(texture->image, texture->mipmaps, pixelFormat);
        return result;
    }
    case AssetType::Mesh: {
        const auto& mesh = prepared.mesh;
        if (!mesh) {
            break;
        }
        std::shared_ptr<const Mesh> result = Mesh::Create(*m_device, *mesh);
        return result;
    }
    case AssetType::Material: {
        const auto& material = prepared.material;
        if (!material) {
            break;
        }
        return std::make_shared<const Material>(
            Material{.baseColor = material->baseColor,
                     .baseColorTexture = Load<Texture>(material->baseColorTexture),
                     .metallic = material->metallic,
                     .roughness = material->roughness,
                     .metallicRoughnessTexture = Load<Texture>(material->metallicRoughnessTexture),
                     .normalTexture = Load<Texture>(material->normalTexture),
                     .normalScale = material->normalScale,
                     .occlusionTexture = Load<Texture>(material->occlusionTexture),
                     .occlusionStrength = material->occlusionStrength,
                     .emissive = material->emissive,
                     .emissiveTexture = Load<Texture>(material->emissiveTexture),
                     .alphaMode = material->alphaMode,
                     .alphaCutoff = material->alphaCutoff,
                     .doubleSided = material->doubleSided,
                     .worldTextureSize = material->worldTextureSize,
                     .nearestFiltering = material->nearestFiltering});
    }
    case AssetType::Model: {
        const auto& nodes = prepared.model;
        if (!nodes) {
            break;
        }
        std::vector<ModelNode> loaded;
        for (const auto& node : *nodes) {
            ModelNode& added = loaded.emplace_back(
                ModelNode{.name = node.name,
                          .transform = node.transform,
                          .parent = node.parent >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(node.parent))
                                                     : std::nullopt});
            added.joint = node.joint;
            added.skeleton = Load<Skeleton>(node.skeleton);
            for (const auto clip : node.animations) {
                added.animations.push_back(Load<AnimationClip>(clip));
            }
            if (node.mesh.IsValid()) {
                added.mesh = Load<Mesh>(node.mesh);
            }
            for (const AssetId material : node.materials) {
                added.materials.push_back(Load<Material>(material));
            }
        }
        return std::make_shared<const Model>(std::move(loaded));
    }
    case AssetType::Sound: {
        auto sound = Detail::DecodeSound(*data);
        if (!sound) {
            break;
        }
        if (sound->streamed) {
            return Sound::CreateStream(std::move(sound->encoded));
        }
        std::vector<float> samples(sound->samples.size());
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<float>(sound->samples[i]) / 32768.0f;
        }
        return std::make_shared<const Sound>(sound->sampleRate, sound->channelCount, std::move(samples));
    }
    case AssetType::Font: {
        if (!prepared.fontAtlas) {
            break;
        }
        std::shared_ptr<const Font> result = Font::Create(*m_device, std::move(*prepared.fontAtlas));
        return result;
    }
    case AssetType::AnimatorController: {
        const auto text = Detail::DecodeScene(*data);
        auto controller = text ? AnimatorController::LoadFromString(*text, this) : std::nullopt;
        return controller ? std::make_shared<const AnimatorController>(std::move(*controller)) : nullptr;
    }
    case AssetType::SpriteAtlas: {
        const auto text = Detail::DecodeScene(*data);
        auto atlas = text ? SpriteAtlas::LoadFromString(*text, this) : std::nullopt;
        return atlas ? std::make_shared<const SpriteAtlas>(std::move(*atlas)) : nullptr;
    }
    case AssetType::Skeleton: {
        auto skeleton = Detail::DecodeSkeleton(*data);
        return skeleton ? std::make_shared<const Skeleton>(std::move(*skeleton)) : nullptr;
    }
    case AssetType::AnimationClip: {
        auto clip = Detail::DecodeAnimationClip(*data);
        return clip ? std::make_shared<const AnimationClip>(std::move(*clip)) : nullptr;
    }
    case AssetType::Script: {
        Detail::BinaryReader reader(*data);
        std::string source = reader.ReadString();
        if (!reader.IsValid() || !reader.IsAtEnd()) {
            break;
        }
        return std::make_shared<const ScriptSource>(std::move(source), std::string(GetPath(id)));
    }
    case AssetType::Prefab:
    case AssetType::Scene:
        Log::GetEngineLogger().Error("{} assets load through the scene layer", GetAssetTypeName(type));
        return nullptr;
    }

    Log::GetEngineLogger().Error("Imported asset {} is corrupt; import it again", id.ToString());
    return nullptr;
}

} // namespace Spot
