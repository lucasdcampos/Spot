#pragma once

#include "assets/asset_id.h"
#include "graphics/color.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Spot {

class Archive;

// What a .material file holds: a material whose texture is referred to by its asset id. The asset manager loads
// these as Material.
struct MaterialSource {
    Color baseColor = Color::White;
    AssetId baseColorTexture;
    bool doubleSided = false;
    float worldTextureSize = 0.0f;

    void Serialize(Archive& archive);

    std::string SaveToString() const;
    bool SaveToFile(const std::filesystem::path& path) const;
    // Return nothing, after logging why, when the text is not a JSON object.
    static std::optional<MaterialSource> LoadFromString(std::string_view text);
    static std::optional<MaterialSource> LoadFromFile(const std::filesystem::path& path);
};

} // namespace Spot
