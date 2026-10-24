#pragma once

#include "spot/assets/asset_loader.h"
#include "spot/assets/asset_type.h"
#include "spot/core/ids/asset_id.h"
#include "spot/core/ids/asset_ref.h"

#include <atomic>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Spot {

struct Skeleton;
struct AnimationClip;
struct AnimatorController;
struct SpriteAtlas;
struct ScriptSource;
class Font;
class Mesh;
struct MeshData;
class Model;
class Sound;
class Texture;
struct Material;

template <typename T>
struct AssetTypeOf;

template <>
struct AssetTypeOf<Texture> {
    static constexpr AssetType Value = AssetType::Texture;
};

template <>
struct AssetTypeOf<Mesh> {
    static constexpr AssetType Value = AssetType::Mesh;
};

// The triangles of a mesh asset in CPU memory, as physics needs them, rather than the GPU mesh renderers draw.
template <>
struct AssetTypeOf<MeshData> {
    static constexpr AssetType Value = AssetType::Mesh;
};

template <>
struct AssetTypeOf<Material> {
    static constexpr AssetType Value = AssetType::Material;
};

template <>
struct AssetTypeOf<Model> {
    static constexpr AssetType Value = AssetType::Model;
};

template <>
struct AssetTypeOf<Sound> {
    static constexpr AssetType Value = AssetType::Sound;
};

template <>
struct AssetTypeOf<Font> {
    static constexpr AssetType Value = AssetType::Font;
};

template <>
struct AssetTypeOf<Skeleton> {
    static constexpr AssetType Value = AssetType::Skeleton;
};

template <>
struct AssetTypeOf<AnimationClip> {
    static constexpr AssetType Value = AssetType::AnimationClip;
};

template <>
struct AssetTypeOf<AnimatorController> {
    static constexpr AssetType Value = AssetType::AnimatorController;
};

template <>
struct AssetTypeOf<SpriteAtlas> {
    static constexpr AssetType Value = AssetType::SpriteAtlas;
};

template <>
struct AssetTypeOf<ScriptSource> {
    static constexpr AssetType Value = AssetType::Script;
};

namespace Detail {
class AssetPak;
struct PreparedAsset;

// What an asynchronous load shares between the thread that reads the asset and the manager, which makes it.
struct AsyncLoad {
    AssetId id;
    AssetType type = AssetType::Texture;
    std::atomic<bool> prepared = false;
    std::shared_ptr<void> asset;
    bool done = false;
    std::shared_ptr<const void> result;
};

// A preload running in the background (see AssetManager::StartPreload()).
struct PreloadTask {
    std::vector<AssetId> ids;
    std::shared_ptr<const std::unordered_map<AssetId, AssetType>> types;
    std::shared_ptr<void> waves;
    std::atomic<bool> ready = false;
};
} // namespace Detail

// An asset loading in the background (see AssetManager::LoadAsync()): done once AssetManager::Update() made it,
// which the application calls every frame.
template <typename T>
class AssetRequest {
public:
    AssetRequest() = default;
    explicit AssetRequest(std::shared_ptr<Detail::AsyncLoad> load) : m_load(std::move(load)) {}

    bool IsValid() const { return m_load != nullptr; }
    bool IsDone() const { return m_load && m_load->done; }
    // A reference without an asset until it is done, or when it failed to load.
    AssetRef<T> Get() const {
        return m_load && m_load->done ? AssetRef<T>(m_load->id, std::static_pointer_cast<const T>(m_load->result))
                                      : AssetRef<T>();
    }

private:
    std::shared_ptr<Detail::AsyncLoad> m_load;
};

class AssetManager;

// The types Load() can load.
template <typename T>
concept LoadableAsset = requires { AssetTypeOf<T>::Value; };

// Loads imported assets from an import directory, as written by AssetDatabase, which is all a shipped game needs.
// Each asset is loaded once and shared while something refers to it; it is freed after the last reference goes.
//
//     auto house = assets.Load<Spot::Model>("models/house.glb");
//
// Assets that need the GPU, such as textures and meshes, only load with a graphics device.
class AssetManager {
public:
    explicit AssetManager(std::filesystem::path importDirectory);
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    const std::filesystem::path& GetImportDirectory() const { return m_importDirectory; }

    // Reads the import directory's manifest again, such as after the asset database imported assets. Loaded
    // assets stay as they are.
    void RefreshManifest();

    bool Contains(AssetId id) const { return m_entries.contains(id); }
    std::optional<AssetType> GetType(AssetId id) const;
    // As AssetDatabase::FindByPath(): "models/house.glb" for a file's asset, "models/house.glb#Roof" for one of its
    // sub-assets. Built-in assets are found too (see BuiltInAssets).
    std::optional<AssetId> FindByPath(std::string_view path) const;
    // Manifest assets of the type, sorted by source path, optionally below a relative directory (including its
    // descendants). Requires neither source files nor loading the assets; built-in assets are not included.
    std::vector<AssetId> GetAssetIds(AssetType type, std::string_view directory = {}) const;

    // Returns a reference without an asset, after logging why, when the id is unknown, of another type or fails to
    // load. The reference keeps the id either way.
    template <LoadableAsset T>
    AssetRef<T> Load(AssetId id) {
        if constexpr (std::is_same_v<T, MeshData>) {
            return AssetRef<T>(id, LoadMeshData(id));
        } else {
            return AssetRef<T>(id, std::static_pointer_cast<const T>(LoadAsset(id, AssetTypeOf<T>::Value)));
        }
    }

    // Starts loading the asset in the background: its file is read and decoded on the job system's threads, and the
    // next Update() after that makes it, on the thread that calls it. What it refers to, such as a material's
    // textures, loads then. An asset already loaded is done at once.
    template <LoadableAsset T>
        requires(!std::is_same_v<T, MeshData>)
    AssetRequest<T> LoadAsync(AssetId id) {
        return AssetRequest<T>(StartAsyncLoad(id, AssetTypeOf<T>::Value));
    }
    // Makes the assets whose background loads are read, in the order they were asked for. The application calls it
    // every frame.
    void Update();

    // Loads the assets and those they refer to, as a scene about to be instantiated does with the assets it names:
    // their files are read and decoded together on the job system's threads, then made here. Returns what it loaded,
    // which stays loaded while the caller keeps it, so loads of the same assets after it find them ready.
    std::vector<std::shared_ptr<const void>> Preload(std::span<const AssetId> ids);
    // The same in the background: the files are read and decoded on the job system's threads while this one goes on,
    // and FinishPreload() makes the assets, once IsReady() says the reading is done, or after waiting for it. The
    // manifest must not be refreshed meanwhile.
    std::shared_ptr<Detail::PreloadTask> StartPreload(std::span<const AssetId> ids);
    static bool IsReady(const Detail::PreloadTask& task) { return task.ready.load(); }
    std::vector<std::shared_ptr<const void>> FinishPreload(Detail::PreloadTask& task);
    bool IsLoaded(AssetId id) const;
    // Preloads the assets text saved by the engine, such as a scene, names by their ids.
    std::vector<std::shared_ptr<const void>> PreloadReferencedBy(std::string_view text);

    // The CPU copy of a mesh asset, as Load<MeshData>() gives it: it loads without a graphics device, and is shared
    // while something refers to it, apart from the GPU mesh of the same asset.
    std::shared_ptr<const MeshData> LoadMeshData(AssetId id);

    template <LoadableAsset T>
    AssetRef<T> Load(std::string_view path) {
        const auto id = FindByPath(path);
        if (!id) {
            LogUnknownPath(path);
            return {};
        }
        return Load<T>(*id);
    }

    // Makes the next Load() of these assets read their imported files again, as after they were imported again (see
    // AssetDatabase::GetChanged()), along with the loaded assets that refer to them, such as the materials of a
    // changed texture. What refers to the old ones keeps them until it lets them go. Returns every asset that will
    // load anew, for references to them to be loaded again (see ReloadAssetReferences()).
    std::vector<AssetId> Reload(std::span<const AssetId> assets);

    // The imported data of an asset of the type, as its loader receives it, for assets that are not shared, such as
    // scenes, which each instantiation makes anew. Nothing, after logging why, when it is missing or of another type.
    std::optional<std::vector<std::byte>> ReadImportedData(AssetId id, AssetType type) const;
    // The asset's path, as FindByPath() takes it; empty for unknown assets.
    std::string_view GetPath(AssetId id) const;

    // Register before starting loads. Registrations belong only to this manager; the latest for a type wins.
    void RegisterLoader(AssetType type, AssetLoader loader);

private:
    struct Entry {
        AssetType type;
        std::string path;
        std::weak_ptr<const void> loaded;
        // Mesh assets only: the CPU copy.
        std::weak_ptr<const MeshData> meshData = {};
        // Kept after the asset is freed, so it still loads anew when one of them is reloaded.
        std::vector<AssetId> dependencies = {};
    };

    std::shared_ptr<const void> LoadAsset(AssetId id, AssetType type);
    // Reads and decodes the asset, as far as that goes without the GPU or other assets; safe on any thread.
    Detail::PreparedAsset Prepare(AssetId id, AssetType type) const;
    std::shared_ptr<const void> Finish(Detail::PreparedAsset prepared);
    // Records a loaded asset, or logs why it did not load; returns it either way.
    std::shared_ptr<const void> Keep(AssetId id, AssetType type, std::shared_ptr<const void> asset);
    // Calls fn for each asset the prepared one refers to; text names assets by id, of which those known count.
    void ForEachReference(const Detail::PreparedAsset& asset, const std::function<bool(AssetId)>& known,
                          const std::function<void(AssetId)>& fn) const;
    static void ForEachIdInText(std::span<const std::byte> text, const std::function<bool(AssetId)>& known,
                                const std::function<void(AssetId)>& fn);
    // The types of the assets not loaded, as a preload reads them.
    std::unordered_map<AssetId, AssetType> GetUnloadedTypes() const;
    std::vector<std::vector<Detail::PreparedAsset>>
    PrepareWaves(std::span<const AssetId> ids, const std::unordered_map<AssetId, AssetType>& types) const;
    std::vector<std::shared_ptr<const void>> FinishWaves(std::vector<std::vector<Detail::PreparedAsset>>& waves);
    static bool IsBuiltIn(AssetId id);
    std::shared_ptr<Detail::AsyncLoad> StartAsyncLoad(AssetId id, AssetType type);
    // The asset's data after its header, from the pak when there is one, or from its imported file.
    std::optional<std::vector<std::byte>> ReadData(AssetId id, AssetType type) const;
    std::shared_ptr<const void> Create(AssetId id, AssetType type);
    static void LogUnknownPath(std::string_view path);

    std::filesystem::path m_importDirectory;
    std::unordered_map<AssetType, AssetLoader> m_loaders;
    std::unique_ptr<Detail::AssetPak> m_pak;
    std::deque<std::shared_ptr<Detail::AsyncLoad>> m_pending;
    std::vector<std::shared_ptr<Detail::PreloadTask>> m_preloads;
    std::unordered_map<AssetId, Entry> m_entries;
    std::unordered_map<std::string, AssetId> m_paths;
};

} // namespace Spot
