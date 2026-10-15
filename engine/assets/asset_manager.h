#pragma once

#include "assets/asset_ref.h"
#include "assets/asset_type.h"
#include "core/asset_id.h"

#include <cstddef>
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

struct Skeleton;
struct AnimationClip;
struct AnimatorController;
struct SpriteAtlas;
class Font;
class GraphicsDevice;
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

class AssetManager;

// Loads the assets of a type the asset layer does not define itself, such as the prefabs the scene layer registers.
struct AssetLoader {
    // Makes the asset from its imported data; null, after logging why, when the data is not valid.
    std::shared_ptr<const void> (*load)(AssetManager& assets, AssetId id, std::span<const std::byte> data) = nullptr;
    // The assets the loaded asset was made from, so it loads anew when they are reloaded; none when unset.
    std::vector<AssetId> (*dependencies)(const void* asset) = nullptr;
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

    // Makes every manager load assets of the type with the loader, as the layers above the asset layer do for
    // their own types. The latest registration for a type wins.
    static void RegisterLoader(AssetType type, AssetLoader loader);

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
    std::shared_ptr<const void> Create(AssetId id, AssetType type);
    static void LogUnknownPath(std::string_view path);

    std::filesystem::path m_importDirectory;
    GraphicsDevice* m_device;
    std::unordered_map<AssetId, Entry> m_entries;
    std::unordered_map<std::string, AssetId> m_paths;
};

} // namespace Spot
