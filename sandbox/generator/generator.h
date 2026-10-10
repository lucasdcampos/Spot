#pragma once

#include "output.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Generator {

// A prop the generator makes: models/<name>.gltf, with its buffer beside it, textured by textures/<texture>.png.
struct PropInfo {
    std::string_view name;
    std::string_view texture;
};

// Every prop, in the order they are written.
std::vector<PropInfo> GetProps();
// The names of the sounds the generator makes, each sounds/<name>.wav.
std::vector<std::string_view> GetSoundNames();

// Writes every generated asset into the folder, sandbox/assets/generated in the sandbox, rewriting only the files
// whose content changed.
OutputCounts GenerateSandboxAssets(const std::filesystem::path& directory);

} // namespace Generator
