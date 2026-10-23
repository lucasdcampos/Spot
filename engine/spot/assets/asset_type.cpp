#include "spot/assets/asset_type.h"

#include <array>
#include <cctype>
#include <string>
#include <utility>

namespace Spot {

namespace {

constexpr std::array<std::pair<AssetType, std::string_view>, 13> TypeNames{{
    {AssetType::Texture, "Texture"},
    {AssetType::Mesh, "Mesh"},
    {AssetType::Material, "Material"},
    {AssetType::Model, "Model"},
    {AssetType::Sound, "Sound"},
    {AssetType::Font, "Font"},
    {AssetType::Scene, "Scene"},
    {AssetType::Prefab, "Prefab"},
    {AssetType::Skeleton, "Skeleton"},
    {AssetType::AnimationClip, "AnimationClip"},
    {AssetType::AnimatorController, "AnimatorController"},
    {AssetType::SpriteAtlas, "SpriteAtlas"},
    {AssetType::Script, "Script"},
}};

constexpr std::array<std::pair<std::string_view, AssetType>, 18> Extensions{{
    {".png", AssetType::Texture},
    {".jpg", AssetType::Texture},
    {".jpeg", AssetType::Texture},
    {".bmp", AssetType::Texture},
    {".tga", AssetType::Texture},
    {".gltf", AssetType::Model},
    {".glb", AssetType::Model},
    {".material", AssetType::Material},
    {".wav", AssetType::Sound},
    {".ogg", AssetType::Sound},
    {".ttf", AssetType::Font},
    {".otf", AssetType::Font},
    {".scene", AssetType::Scene},
    {".prefab", AssetType::Prefab},
    {".controller", AssetType::AnimatorController},
    {".atlas", AssetType::SpriteAtlas},
    {".luau", AssetType::Script},
}};

} // namespace

std::string_view GetAssetTypeName(AssetType type) {
    for (const auto& [candidate, name] : TypeNames) {
        if (candidate == type) {
            return name;
        }
    }
    return "Unknown";
}

std::optional<AssetType> ParseAssetType(std::string_view name) {
    for (const auto& [type, candidate] : TypeNames) {
        if (candidate == name) {
            return type;
        }
    }
    return std::nullopt;
}

std::optional<AssetType> GetSourceAssetType(const std::filesystem::path& path) {
    const std::u8string utf8 = path.extension().u8string();
    std::string extension;
    for (const char8_t c : utf8) {
        extension.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    for (const auto& [candidate, type] : Extensions) {
        if (candidate == extension) {
            return type;
        }
    }
    return std::nullopt;
}

} // namespace Spot
