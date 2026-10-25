#pragma once

#include "output.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Generator {

// A prop the generator makes: models/<name>.gltf, with its buffer beside it, of textures/<textures>_color.png,
// _normal.png and _orm.png, which are its own, painted for its shape, or a surface's.
struct PropInfo {
    std::string_view name;
    std::string_view textures;
};

// Every prop, in the order they are written.
std::vector<PropInfo> GetProps();
// The names of the held items' models, each models/<name>.gltf.
std::vector<std::string_view> GetWeaponModelNames();
std::vector<std::string_view> GetRigModelNames();
// The names of the level's scenery models, each models/<name>.gltf.
std::vector<std::string_view> GetLevelModelNames();
// The names of the surfaces, whose textures are textures/<surface>_color.png, _normal.png and _orm.png.
std::vector<std::string_view> GetSurfaceNames();
// The names of the sounds the generator makes, each sounds/<name>.wav.
std::vector<std::string_view> GetSoundNames();

// Writes every generated asset into the folder, sandbox/assets/generated in the sandbox, rewriting only the files
// whose content changed. Surfaces' and props' textures are painted textureDivisor times smaller on each side, as tests
// paint them to be quick; the same at any size, only less detailed.
OutputCounts GenerateSandboxAssets(const std::filesystem::path& directory, std::uint32_t textureDivisor = 1);

} // namespace Generator
