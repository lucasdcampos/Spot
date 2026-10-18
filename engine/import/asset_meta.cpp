#include "import/asset_meta.h"

#include "core/archive.h"
#include "core/archive_state.h"
#include "core/log.h"
#include "core/paths.h"

#include <fstream>
#include <iterator>
#include <utility>

namespace Spot {

using nlohmann::json;

namespace {

std::string Dump(const json& document) {
    return document.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

// Writes the settings of the type into the archive, under "settings" when the name is set.
void SerializeSettings(AssetMeta& meta, Archive& archive, std::string_view name) {
    switch (meta.type) {
    case AssetType::Texture:
        archive.Field(name, meta.texture);
        break;
    case AssetType::Model:
        archive.Field(name, meta.model);
        break;
    case AssetType::Font:
        archive.Field(name, meta.font);
        break;
    case AssetType::Sound:
        archive.Field(name, meta.sound);
        break;
    default:
        break;
    }
}

} // namespace

void TextureImportSettings::Serialize(Archive& archive) {
    archive.Field("mipmaps", mipmaps, {.tooltip = "Smaller copies that keep the texture smooth from afar"});
    archive.Field("srgb", srgb, {.tooltip = "Holds colors; off for data kept as it is, such as LUTs and normal maps"});
    archive.Field("compress", compress,
                  {.tooltip = "Block-compressed when it has mipmaps: far less memory, a little blur"});
    archive.Field(
        "keepAlphaCoverage", keepAlphaCoverage,
        {.tooltip = "For cutouts such as leaves: compressed mipmaps stay as covered, not thinning from afar"});
}

void MaterialOverride::Serialize(Archive& archive) {
    archive.Field("key", key, {.tooltip = "Which of the model's materials, such as material:0"});
    archive.Field("material", material, {.assetType = AssetType::Material});
}

void AnimationEventImportSettings::Serialize(Archive& archive) {
    archive.Field("clip", clip);
    archive.Field("events", events);
}

void ModelImportSettings::Serialize(Archive& archive) {
    archive.Field("scale", scale,
                  {.min = 0.0001f, .max = 10000.0f, .speed = 0.001f, .tooltip = "Such as 0.01 for centimeters"});
    archive.Field("materials", materials, {.tooltip = "Materials of the model replaced by material assets"});
    archive.Field("animations", animations);
}

void FontImportSettings::Serialize(Archive& archive) {
    archive.Field("pixelSize", pixelSize,
                  {.min = 4.0f, .max = 512.0f, .speed = 0.5f, .tooltip = "The size glyphs are drawn at, in pixels"});
    archive.Field("extraSizes", extraSizes, {.tooltip = "More sizes of the font, each a sub-asset"});
}

void SoundImportSettings::Serialize(Archive& archive) {
    archive.Field("stream", stream,
                  {.tooltip = "Keeps the sound encoded and decodes it while it plays: for music and long ambience"});
}

void SubAssetId::Serialize(Archive& archive) {
    archive.Field("key", key);
    archive.Field("id", id);
}

AssetId AssetMeta::GetOrAddSubAssetId(std::string_view key) {
    for (const auto& subAsset : subAssets) {
        if (subAsset.key == key) {
            return subAsset.id;
        }
    }
    subAssets.push_back({std::string(key), AssetId::Generate()});
    return subAssets.back().id;
}

std::string AssetMeta::GetSettingsString() const {
    json document = json::object();
    Detail::ArchiveState state{.loading = false, .nodes = {&document}};
    Archive archive(state);
    AssetMeta copy = *this;
    SerializeSettings(copy, archive, {});
    return document.dump();
}

std::string AssetMeta::SaveToString() const {
    json document = json::object();
    Detail::ArchiveState state{.loading = false, .nodes = {&document}};
    Archive archive(state);

    AssetMeta copy = *this;
    archive.Field("id", copy.id);
    std::string typeName(GetAssetTypeName(type));
    archive.Field("type", typeName);
    SerializeSettings(copy, archive, "settings");
    if (!copy.subAssets.empty()) {
        archive.Field("subAssets", copy.subAssets);
    }
    return Dump(document);
}

bool AssetMeta::SaveToFile(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::binary);
    file << SaveToString();
    if (!file) {
        Log::GetEngineLogger().Error("Could not write meta file '{}'", PathToUtf8(path));
        return false;
    }
    return true;
}

std::optional<AssetMeta> AssetMeta::LoadFromString(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object()) {
        Log::GetEngineLogger().Error("Meta file is not a JSON object");
        return std::nullopt;
    }

    Detail::ArchiveState state{.loading = true, .nodes = {&document}};
    Archive archive(state);
    AssetMeta meta;
    archive.Field("id", meta.id);
    std::string typeName;
    archive.Field("type", typeName);
    const auto type = ParseAssetType(typeName);
    if (!meta.id.IsValid() || !type) {
        Log::GetEngineLogger().Error("Meta file has no valid id and type");
        return std::nullopt;
    }
    meta.type = *type;
    SerializeSettings(meta, archive, "settings");
    archive.Field("subAssets", meta.subAssets);
    return meta;
}

std::optional<AssetMeta> AssetMeta::LoadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Log::GetEngineLogger().Error("Could not open meta file '{}'", PathToUtf8(path));
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto meta = LoadFromString(text);
    if (!meta) {
        Log::GetEngineLogger().Error("Could not read meta file '{}'", PathToUtf8(path));
    }
    return meta;
}

std::filesystem::path AssetMeta::GetMetaPath(const std::filesystem::path& sourcePath) {
    std::filesystem::path path = sourcePath;
    path += ".meta";
    return path;
}

} // namespace Spot
