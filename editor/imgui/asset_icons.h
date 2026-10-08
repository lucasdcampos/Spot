#pragma once

#include "assets/asset_browser_model.h"

#include <spot.h>

#include <imgui.h>

#include <string_view>

// Painted asset browser icons: small illustrations built from draw list shapes with gradients, highlights and soft
// shadows instead of icon font glyphs. Each painter fills the square at min with the side size and stays legible from
// 48 to 160 pixels. Documents share one look, a page with a folded corner and a badge.
namespace Spot::Editor::AssetIcons {

// The type's color, for its icon's accents and the line under its tile.
ImVec4 GetTypeColor(AssetType type);
// The same for plain files.
ImVec4 GetFileColor(FileKind kind);

// A folder with a tabbed back panel; papers peek out of it when it holds anything.
void DrawFolder(ImDrawList* drawList, ImVec2 min, float size, bool hasContents);
// The type's icon, for assets that have no picture of their own yet.
void DrawAsset(ImDrawList* drawList, ImVec2 min, float size, AssetType type);
// A plain file, by its name: code, a build script, or any other file with its extension on a badge.
void DrawFile(ImDrawList* drawList, ImVec2 min, float size, std::string_view name);

} // namespace Spot::Editor::AssetIcons
