#pragma once

#include "output.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Generator {

// A prop the generator makes: models/<name>.gltf, with its buffer beside it, colored by textures/<texture>.png, with
// the normal and ORM textures of its surface, textures/<surface>_normal.png and textures/<surface>_orm.png.
struct PropInfo {
    std::string_view name;
    std::string_view texture;
    std::string_view surface;
};

// Every prop, in the order they are written.
std::vector<PropInfo> GetProps();
// The names of the held items' models, each models/<name>.gltf.
std::vector<std::string_view> GetWeaponModelNames();
std::vector<std::string_view> GetRigModelNames();
// The names of the surfaces, whose textures are textures/<surface>_color.png, _normal.png and _orm.png.
std::vector<std::string_view> GetSurfaceNames();
// The names of the sounds the generator makes, each sounds/<name>.wav.
std::vector<std::string_view> GetSoundNames();

// Writes every generated asset into the folder, sandbox/assets/generated in the sandbox, rewriting only the files
// whose content changed.
OutputCounts GenerateSandboxAssets(const std::filesystem::path& directory);

} // namespace Generator
