#pragma once

#include "core/asset_id.h"

// Assets every asset manager has without any file, under fixed ids, so scenes can save references to them. Their
// paths are "builtin/cube", "builtin/sphere", "builtin/plane", "builtin/grid" and "builtin/checker".
namespace Spot::BuiltInAssets {

// MeshData::CreateCube(): a unit cube centered on the origin.
inline constexpr AssetId CubeMesh{0, 1};
// MeshData::CreateSphere(): a sphere of diameter 1 centered on the origin.
inline constexpr AssetId SphereMesh{0, 2};
// MeshData::CreatePlane(): a unit square on the XZ plane, facing +Y.
inline constexpr AssetId PlaneMesh{0, 3};
// Image::CreateGrid(), mipmapped. With Material::worldTextureSize it keeps its size on blocks of any size, for
// prototyping levels.
inline constexpr AssetId GridTexture{0, 4};
// Image::CreateChecker(), mipmapped: shows how a mesh's UVs stretch.
inline constexpr AssetId CheckerTexture{0, 5};

} // namespace Spot::BuiltInAssets
