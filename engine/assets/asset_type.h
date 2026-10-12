#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace Spot {

enum class AssetType {
    Texture,
    Mesh,
    Material,
    Model,
    Sound,
    Font,
    Scene,
    Prefab,
    Skeleton,
    AnimationClip,
    AnimatorController,
};

// The name used in .meta and manifest files, such as "Texture".
std::string_view GetAssetTypeName(AssetType type);
std::optional<AssetType> ParseAssetType(std::string_view name);

// The type of asset a source file holds, from its extension, ignoring case:
//   Texture  .png .jpg .jpeg .bmp .tga        Sound  .wav .ogg
//   Model    .gltf .glb                       Font   .ttf .otf
//   Material .material                        Scene  .scene
//   Prefab   .prefab
// Nothing for files that are not assets, such as .meta files, licenses or a .gltf's buffers.
std::optional<AssetType> GetSourceAssetType(const std::filesystem::path& path);

} // namespace Spot
