#include "spot/assets/material_source.h"

#include "spot/assets/asset_type.h"

#include "spot/assets/asset_archive.h"
#include "spot/assets/asset_file.h"
#include "spot/core/debug/log.h"
#include "spot/core/io/archive.h"
#include "spot/core/io/archive_state.h"
#include "spot/core/io/paths.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

namespace Spot {

using nlohmann::json;

void MaterialSource::Serialize(Archive& archive) {
    archive.Field("nearestFiltering", nearestFiltering, {.tooltip = "Keeps pixel art texels sharp in every pass"});
    archive.Field("baseColor", baseColor, {.tooltip = "The surface's color, which multiplies the texture's"});
    archive.Field("baseColorTexture", baseColorTexture,
                  {.tooltip = "The surface's colors, from the mesh's UVs", .assetType = AssetType::Texture});
    archive.Field(
        "metallic", metallic,
        {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .tooltip = "0 for dielectrics such as wood, 1 for metals"});
    archive.Field(
        "roughness", roughness,
        {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .tooltip = "From 0 for a mirror to 1 for a matte surface"});
    archive.Field("metallicRoughnessTexture", metallicRoughnessTexture,
                  {.tooltip = "Roughness in green and metallic in blue, multiplying the values above; import it with "
                              "sRGB off",
                   .assetType = AssetType::Texture});
    archive.Field(
        "normalTexture", normalTexture,
        {.tooltip = "A tangent-space normal map, green up; import it with sRGB off", .assetType = AssetType::Texture});
    archive.Field(
        "normalScale", normalScale,
        {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "How strongly the normal map tilts the surface"});
    archive.Field("occlusionTexture", occlusionTexture,
                  {.tooltip = "How much ambient light reaches each point, in red; import it with sRGB off",
                   .assetType = AssetType::Texture});
    archive.Field("occlusionStrength", occlusionStrength,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .tooltip = "How much the occlusion map darkens"});
    archive.Field("emissive", emissive,
                  {.hdr = true, .tooltip = "Light the surface gives off itself; above 1 it glows through bloom"});
    archive.Field("emissiveTexture", emissiveTexture,
                  {.tooltip = "Where and in which colors the surface glows, multiplying the emissive color",
                   .assetType = AssetType::Texture});
    static constexpr std::array<std::string_view, 3> AlphaModes = {"Opaque", "Cutout", "Transparent"};
    archive.Field("alphaMode", alphaMode,
                  {.enumNames = AlphaModes,
                   .tooltip = "Whether alpha is ignored, cuts out pixels below the cutoff, or lets what is behind "
                              "show through"});
    archive.Field(
        "alphaCutoff", alphaCutoff,
        {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .tooltip = "Cutout pixels below this alpha are not drawn"});
    archive.Field("doubleSided", doubleSided, {.tooltip = "Draws back faces too, for thin surfaces like leaves"});
    archive.Field("worldTextureSize", worldTextureSize,
                  {.min = 0.0f,
                   .max = 1000.0f,
                   .speed = 0.01f,
                   .tooltip = "When above 0, the textures repeat every this many units in the world instead of "
                              "following the UVs, keeping their size however the mesh is scaled"});
}

std::string MaterialSource::SaveToString() const {
    json document = json::object();
    Detail::ArchiveState state{.loading = false, .nodes = {&document}};
    Archive archive(state);
    MaterialSource copy = *this;
    copy.Serialize(archive);
    return document.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

bool MaterialSource::SaveToFile(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::binary);
    file << SaveToString();
    if (!file) {
        Log::GetEngineLogger().Error("Could not write material '{}'", PathToUtf8(path));
        return false;
    }
    return true;
}

std::optional<MaterialSource> MaterialSource::LoadFromString(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object()) {
        Log::GetEngineLogger().Error("Material is not a JSON object");
        return std::nullopt;
    }
    Detail::ArchiveState state{.loading = true, .nodes = {&document}};
    Archive archive(state);
    MaterialSource material;
    material.Serialize(archive);
    // Blinn-Phong highlights of exponent n are about as sharp as GGX ones of alpha sqrt(2 / (n + 2)), and alpha is the
    // square of roughness. Materials without highlights were matte.
    if (!document.contains("roughness") && (document.contains("specular") || document.contains("shininess"))) {
        const float specular = document.value("specular", 0.0f);
        const float shininess = std::max(document.value("shininess", 32.0f), 0.0f);
        material.roughness = specular > 0.0f ? std::pow(2.0f / (shininess + 2.0f), 0.25f) : 1.0f;
    }
    return material;
}

std::optional<MaterialSource> MaterialSource::LoadFromFile(const std::filesystem::path& path) {
    const auto bytes = Detail::ReadFileBytes(path);
    if (!bytes) {
        Log::GetEngineLogger().Error("Could not open material '{}'", PathToUtf8(path));
        return std::nullopt;
    }
    auto material = LoadFromString({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    if (!material) {
        Log::GetEngineLogger().Error("Could not read material '{}'", PathToUtf8(path));
    }
    return material;
}

} // namespace Spot
