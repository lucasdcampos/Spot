#pragma once

#include "math/vector.h"

#include <cstdint>
#include <vector>

namespace Spot {

struct MeshVertex {
    Vector3 position;
    Vector3 normal;
    // From (0, 0) at the texture's top-left to (1, 1) at its bottom-right.
    Vector2 uv;
    // The direction the texture's u grows along the surface, perpendicular to the normal, and in w, 1 or -1, the side
    // v grows on: the bitangent is cross(normal, tangent.xyz) * w. Normal maps are read in this space.
    Vector4 tangent = {};
};

// A sphere holding something, such as every vertex of a mesh, so renderers can skip it when it is out of view.
struct BoundingSphere {
    Vector3 center = {};
    float radius = 0.0f;
};

// A box aligned with the axes holding something, such as every vertex of a mesh.
struct BoundingBox {
    Vector3 min = {};
    Vector3 max = {};

    Vector3 GetCenter() const { return (min + max) * 0.5f; }
    Vector3 GetSize() const { return max - min; }
};

// Indexed triangles in CPU memory, with front faces winding counterclockwise. Mesh::Create() sends them to the GPU.
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;

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

    // Centered on the box around the vertices, so it fits them closely without being the smallest sphere possible.
    BoundingSphere GetBounds() const;
    // The smallest box aligned with the axes around the vertices; empty at the origin without any.
    BoundingBox GetBoundingBox() const;
};

} // namespace Spot
