#pragma once

#include <spot.h>

#include <imgui.h>

// Painted asset browser icons: small illustrations built from draw list shapes with gradients, highlights and soft
// shadows instead of icon font glyphs. Each painter fills the square at min with the side size and stays legible from
// 48 to 160 pixels. Documents share one look, a page with a folded corner and a badge.
namespace Spot::Editor::AssetIcons {

// The type's color, for its icon's accents and the line under its tile.
ImVec4 GetTypeColor(AssetType type);

// A folder with a tabbed back panel; papers peek out of it when it holds anything.
void DrawFolder(ImDrawList* drawList, ImVec2 min, float size, bool hasContents);
// The type's icon, for assets that have no picture of their own yet.
void DrawAsset(ImDrawList* drawList, ImVec2 min, float size, AssetType type);

} // namespace Spot::Editor::AssetIcons
