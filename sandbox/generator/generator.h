#pragma once

#include "output.h"

#include <filesystem>
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

// Writes every generated asset into the folder, sandbox/assets/generated in the sandbox, rewriting only the files
// whose content changed.
OutputCounts GenerateSandboxAssets(const std::filesystem::path& directory);

} // namespace Generator
