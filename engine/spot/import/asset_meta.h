#pragma once

#include "spot/animation/animation_data.h"
#include "spot/assets/asset_type.h"
#include "spot/core/ids/asset_id.h"

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
    // For cutout surfaces such as leaves, when compressed: scales the alpha of each mipmap so as much of it stays at
    // half alpha or more as in the image, rather than thinning out from afar.
    bool keepAlphaCoverage = false;

    void Serialize(Archive& archive);
};

// A material of a model replaced by a material asset, such as one extracted with AssetDatabase::ExtractMaterial().
struct MaterialOverride {
    // The key of the model's material among its sub-assets, such as "material:0".
    std::string key;
    AssetId material;

    void Serialize(Archive& archive);
};

// How to import one of a model's animation clips, found by its name in the file. Several settings for the same clip
// split it into clips of their own, each a part of it under its own name, as a file with all of a character's moves
// in one take needs.
struct ClipImportSettings {
    std::string clip;
    std::vector<AnimationEvent> events;
    // The clip's name once imported; empty keeps the file's.
    std::string name;
    // The part of the clip to keep, in seconds from its start; an end of 0 keeps the rest of it.
    float start = 0.0f;
    float end = 0.0f;
    // See AnimationClip::retargetTranslations and AnimationClip::rootMotionRotation.
    bool retargetTranslations = true;
    bool rootMotionRotation = true;

    void Serialize(Archive& archive);
};

struct ModelImportSettings {
    // Scales the whole model, such as 0.01 for files made in centimeters.
    float scale = 1.0f;
    std::vector<MaterialOverride> materials;
    std::vector<ClipImportSettings> animations;

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
