#pragma once

#include "spot/core/geometry/mesh_data.h"

#include <cstdint>

// The meshes of the built-in assets, also for code that builds meshes of its own.
namespace Spot::PrimitiveMeshes {

// Centered on the origin, with every face mapping the whole texture.
MeshData CreateCube(float size = 1.0f);
// A square on the XZ plane, facing +Y.
MeshData CreatePlane(float size = 1.0f);
// The texture wraps around once, its top at the north pole (+Y).
MeshData CreateSphere(float radius = 0.5f, std::uint32_t segments = 32, std::uint32_t rings = 16);

} // namespace Spot::PrimitiveMeshes
