#pragma once

// Engine-internal: reads FBX and OBJ models, through ufbx, into the ModelData the model importer writes, as
// ModelData::Load() does for glTF. Only importing needs these formats, so games never carry their reader.

#include "spot/render/model_data.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Spot::Detail {

struct FbxModel {
    ModelData model;
    // Per image of the model, the file it was read from; empty for images the model holds itself.
    std::vector<std::filesystem::path> imageFiles;
    // Per image, the name of its file, as the model gives it, such as "Body_diffuse.png"; empty when it has none.
    std::vector<std::string> imageNames;
    // Other files the model read, such as an OBJ's materials.
    std::vector<std::filesystem::path> dependencies;
};

// Looks for a texture by its file name elsewhere than beside the model, such as among a project's assets; empty when
// there is none.
using TextureFinder = std::function<std::filesystem::path(std::string_view fileName)>;

// Reads the model in meters with Y up, whatever units and axes it was made in. Textures are read from the file when
// it holds them, unless a file of theirs is found: where the model says, beside it, in a "textures" folder beside it,
// in the "<name>.fbm" folder FBX tools extract textures to, or through the finder. Animations bake to linear keys,
// named after the file when it has one under a name tools give every animation, such as "mixamo.com". Returns
// nothing, after logging why, when the file cannot be read.
std::optional<FbxModel> ReadFbx(const std::filesystem::path& path, const TextureFinder& findTexture = {});

// Writes the textures the model holds into the folder, under their own file names, skipping names already there, and
// returns the files written.
std::vector<std::filesystem::path> ExtractFbxTextures(const std::filesystem::path& path,
                                                      const std::filesystem::path& folder);

} // namespace Spot::Detail
