#pragma once

#include "assets/asset_id.h"
#include "assets/asset_ref.h"
#include "assets/asset_type.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Spot {

class Font;
class GraphicsDevice;
class Mesh;
struct MeshData;
class Model;
class Prefab;
class Scene;
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
struct AssetTypeOf<Prefab> {
    static constexpr AssetType Value = AssetType::Prefab;
};

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
    explicit AssetManager(std::filesystem::path importDirectory, GraphicsDevice* device = nullptr);
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

    // Scenes are not shared: each call makes a new scene from the asset, whose asset references load through this
    // manager. Null, after logging why, when the asset is missing or not a scene.
    std::unique_ptr<Scene> InstantiateScene(AssetId id);
    std::unique_ptr<Scene> InstantiateScene(std::string_view path);

private:
    struct Entry {
        AssetType type;
        std::string path;
        std::weak_ptr<const void> loaded;
        // Mesh assets only: the CPU copy.
        std::weak_ptr<const MeshData> meshData = {};
    };

    std::shared_ptr<const void> LoadAsset(AssetId id, AssetType type);
    std::shared_ptr<const void> Create(AssetId id, AssetType type);
    static void LogUnknownPath(std::string_view path);

    std::filesystem::path m_importDirectory;
    GraphicsDevice* m_device;
    std::unordered_map<AssetId, Entry> m_entries;
    std::unordered_map<std::string, AssetId> m_paths;
};

} // namespace Spot
