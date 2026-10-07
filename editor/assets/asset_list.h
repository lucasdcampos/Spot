#pragma once

#include <spot.h>

#include <optional>
#include <string>
#include <vector>

namespace Spot::Editor {

class EditorProject;

// An asset as the editor lists it in pickers and browsers.
struct AssetEntry {
    AssetId id;
    AssetType type = AssetType::Texture;
    // What it is called: a file's name, such as "crate.png", or a sub-asset's, such as "Roof".
    std::string name;
    // Where it comes from, such as "models/house.glb" or "builtin/cube".
    std::string path;
    bool builtIn = false;
};

// The assets of the type, or of any type: the built-in ones first, then the project's, in the order of their paths.
// The project may be null.
std::vector<AssetEntry> ListAssets(EditorProject* project, std::optional<AssetType> type = std::nullopt);
// Nothing for the null id and for ids neither the project nor the built-in assets have.
std::optional<AssetEntry> FindAsset(EditorProject* project, AssetId id);

const char* GetAssetTypeIcon(AssetType type);

} // namespace Spot::Editor
