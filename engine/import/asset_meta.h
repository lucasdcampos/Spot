#pragma once

#include "animation/animation_data.h"
#include "assets/asset_type.h"
#include "core/asset_id.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

class Archive;

struct TextureImportSettings {
    // See TextureDesc::mipmaps.
    bool mipmaps = true;
    // Whether the image holds colors, as most do, which shaders read as linear light; off for data kept as it is,
    // such as LUTs and normal maps.
    bool srgb = true;
    // Stores it block-compressed, a quarter or an eighth of the memory, when it has mipmaps and its sides are
    // multiples of 4; textures without mipmaps, such as pixel art, keep their exact pixels either way.
    bool compress = true;

    void Serialize(Archive& archive);
};

// A material of a model replaced by a material asset, such as one extracted with AssetDatabase::ExtractMaterial().
struct MaterialOverride {
    // The key of the model's material among its sub-assets, such as "material:0".
    std::string key;
    AssetId material;

    void Serialize(Archive& archive);
};

struct AnimationEventImportSettings {
    std::string clip;
    std::vector<AnimationEvent> events;
    void Serialize(Archive& archive);
};

struct ModelImportSettings {
    // Scales the whole model, such as 0.01 for files made in centimeters.
    float scale = 1.0f;
    std::vector<MaterialOverride> materials;
    std::vector<AnimationEventImportSettings> animations;

    void Serialize(Archive& archive);
};

struct SoundImportSettings {
    // Keeps the sound encoded and decodes it while it plays, as music and long ambience should; see Sound.
    bool stream = false;

    void Serialize(Archive& archive);
};

struct FontImportSettings {
    // The size glyphs are rasterized at; see Font.
    float pixelSize = 32.0f;
    // More sizes of the same font, each a sub-asset named after its size, such as "48px".
    std::vector<float> extraSizes;

    void Serialize(Archive& archive);
};

// The id of an asset made from part of a source file, such as a mesh of a model, under a key that names that part.
struct SubAssetId {
    std::string key;
    AssetId id;

    void Serialize(Archive& archive);
};

// What the .meta file next to a source asset holds: the asset's id, which never changes, and how to import it. The
// settings of other types are ignored and not saved. Meta files are meant for version control along with their
// source.
struct AssetMeta {
    AssetId id;
    AssetType type = AssetType::Texture;
    TextureImportSettings texture;
    ModelImportSettings model;
    FontImportSettings font;
    SoundImportSettings sound;
    // Kept across imports, so references to the parts of a file survive reimporting it.
    std::vector<SubAssetId> subAssets;

    // The id of the part of the file under this key, created the first time it is asked for.
    AssetId GetOrAddSubAssetId(std::string_view key);

    // The settings of the asset's type alone, as text: importing again is only needed when these change.
    std::string GetSettingsString() const;

    std::string SaveToString() const;
    bool SaveToFile(const std::filesystem::path& path) const;
    // Return nothing, after logging why, when the text is not a meta file with a valid id and type.
    static std::optional<AssetMeta> LoadFromString(std::string_view text);
    static std::optional<AssetMeta> LoadFromFile(const std::filesystem::path& path);

    // The meta file of a source file, which is its path with ".meta" appended.
    static std::filesystem::path GetMetaPath(const std::filesystem::path& sourcePath);
};

} // namespace Spot
