#pragma once

#include "spot/assets/alpha_mode.h"
#include "spot/core/ids/asset_id.h"
#include "spot/core/math/color.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Spot {

class Archive;

// What a .material file holds: a Material whose textures are referred to by their asset ids. The asset manager loads
// these as Material.
struct MaterialSource {
    Color baseColor = Color::White;
    AssetId baseColorTexture = {};
    float metallic = 0.0f;
    float roughness = 1.0f;
    AssetId metallicRoughnessTexture = {};
    AssetId normalTexture = {};
    float normalScale = 1.0f;
    AssetId occlusionTexture = {};
    float occlusionStrength = 1.0f;
    Color emissive = Color::Black;
    AssetId emissiveTexture = {};
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    float worldTextureSize = 0.0f;
    bool nearestFiltering = false;

    void Serialize(Archive& archive);

    std::string SaveToString() const;
    bool SaveToFile(const std::filesystem::path& path) const;
    // Return nothing, after logging why, when the text is not a JSON object. Materials saved before PBR, with specular
    // and shininess, get the roughness that gives their highlights about the same size.
    static std::optional<MaterialSource> LoadFromString(std::string_view text);
    static std::optional<MaterialSource> LoadFromFile(const std::filesystem::path& path);
};

} // namespace Spot
