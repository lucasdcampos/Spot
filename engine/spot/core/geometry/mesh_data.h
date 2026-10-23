#pragma once

#include "spot/core/geometry/bounds.h"
#include "spot/core/vector.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Spot {

struct SkinVertex {
    std::array<std::uint32_t, 4> joints = {};
    Vector4 weights = {1, 0, 0, 0};
};

struct MeshVertex {
    Vector3 position;
    Vector3 normal;
    // From (0, 0) at the texture's top-left to (1, 1) at its bottom-right.
    Vector2 uv;
    // The direction the texture's u grows along the surface, perpendicular to the normal, and in w, 1 or -1, the side
    // of it the texture's top is on: the bitangent, cross(normal, tangent.xyz) * w, points toward lower v. Normal maps
    // are read in this space, as glTF defines it.
    Vector4 tangent = {};
};

// A range of a mesh's indices, drawn with a material of its own, as a glTF mesh's primitives are.
struct SubMesh {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;

    bool operator==(const SubMesh&) const = default;
};

// Indexed triangles in CPU memory, with front faces winding counterclockwise. Mesh::Create() sends them to the GPU.
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Ranges of the indices, which together cover them all; none means one submesh of every index.
    std::vector<SubMesh> submeshes = {};
    // Optional second vertex stream; empty for static meshes.
    std::vector<SkinVertex> skin = {};

    // Centered on the origin, with every face mapping the whole texture.
    static MeshData CreateCube(float size = 1.0f);
    // A square on the XZ plane, facing +Y.
    static MeshData CreatePlane(float size = 1.0f);
    // The texture wraps around once, its top at the north pole (+Y).
    static MeshData CreateSphere(float radius = 0.5f, std::uint32_t segments = 32, std::uint32_t rings = 16);

    // Replaces the normals by the average of the faces around each vertex, weighted by their area. Vertices are not
    // merged, so split vertices keep hard edges.
    void RecalculateNormals();
    // Fills the tangents from the normals and the texture coordinates, with MikkTSpace, as glTF's normal maps are
    // baked: vertices whose triangles need different tangents, as across a seam of the texture, are split.
    void GenerateTangents();

    // The submeshes, or one of every index when there are none.
    std::vector<SubMesh> GetSubmeshes() const;
    // Adds the mesh's vertices and triangles after these, as a submesh of its own.
    void AppendSubmesh(const MeshData& mesh);

    // Centered on the box around the vertices, so it fits them closely without being the smallest sphere possible.
    BoundingSphere GetBounds() const;
    // The smallest box aligned with the axes around the vertices; empty at the origin without any.
    BoundingBox GetBoundingBox() const;
};

} // namespace Spot
