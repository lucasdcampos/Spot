#include "graphics/mesh_data.h"

#include "math/angles.h"

#include <mikktspace.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Spot {

MeshData MeshData::CreateCube(float size) {
    struct Face {
        Vector3 normal;
        Vector3 right;
        Vector3 up;
    };
    // Right and up are the face's texture axes seen from outside, so right x up points out.
    constexpr std::array<Face, 6> Faces{{
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    }};

    const float half = size / 2.0f;
    MeshData mesh;
    for (const auto& face : Faces) {
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        const Vector3 center = face.normal * half;
        mesh.vertices.push_back({center - (face.right - face.up) * half, face.normal, {0.0f, 0.0f}});
        mesh.vertices.push_back({center + (face.right + face.up) * half, face.normal, {1.0f, 0.0f}});
        mesh.vertices.push_back({center + (face.right - face.up) * half, face.normal, {1.0f, 1.0f}});
        mesh.vertices.push_back({center - (face.right + face.up) * half, face.normal, {0.0f, 1.0f}});
        mesh.indices.insert(mesh.indices.end(), {first, first + 3, first + 2, first + 2, first + 1, first});
    }
    mesh.GenerateTangents();
    return mesh;
}

MeshData MeshData::CreatePlane(float size) {
    const float half = size / 2.0f;
    const Vector3 up{0.0f, 1.0f, 0.0f};
    MeshData mesh;
    mesh.vertices = {
        {{-half, 0.0f, -half}, up, {0.0f, 0.0f}},
        {{half, 0.0f, -half}, up, {1.0f, 0.0f}},
        {{half, 0.0f, half}, up, {1.0f, 1.0f}},
        {{-half, 0.0f, half}, up, {0.0f, 1.0f}},
    };
    mesh.indices = {0, 3, 2, 2, 1, 0};
    mesh.GenerateTangents();
    return mesh;
}

MeshData MeshData::CreateSphere(float radius, std::uint32_t segments, std::uint32_t rings) {
    segments = segments < 3 ? 3 : segments;
    rings = rings < 2 ? 2 : rings;

    // The seam and the poles repeat vertices so each copy can have its own texture coordinates.
    MeshData mesh;
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float polar = v * Pi;
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float azimuth = u * 2.0f * Pi;
            const Vector3 normal{std::sin(polar) * std::sin(azimuth), std::cos(polar),
                                 std::sin(polar) * std::cos(azimuth)};
            mesh.vertices.push_back({normal * radius, normal, {u, v}});
        }
    }

    const std::uint32_t stride = segments + 1;
    for (std::uint32_t ring = 0; ring < rings; ++ring) {
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const std::uint32_t topLeft = ring * stride + segment;
            const std::uint32_t bottomLeft = topLeft + stride;
            mesh.indices.insert(mesh.indices.end(),
                                {topLeft, bottomLeft, bottomLeft + 1, bottomLeft + 1, topLeft + 1, topLeft});
        }
    }
    mesh.GenerateTangents();
    return mesh;
}

namespace {

// MikkTSpace's view of a mesh: its triangles, and the tangent of each of their corners.
struct TangentMesh {
    const MeshData& mesh;
    std::vector<Vector4> cornerTangents;

    const MeshVertex& Corner(int face, int corner) const {
        return mesh.vertices[mesh.indices[static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(corner)]];
    }
};

TangentMesh& GetTangentMesh(const SMikkTSpaceContext* context) {
    return *static_cast<TangentMesh*>(context->m_pUserData);
}

// A tangent perpendicular to the normal, for surfaces whose texture coordinates give none.
Vector4 MakeAnyTangent(Vector3 normal) {
    const Vector3 axis = std::abs(normal.x) < 0.9f ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 1.0f, 0.0f};
    const Vector3 tangent = Normalize(axis - normal * Dot(axis, normal));
    return {tangent.x, tangent.y, tangent.z, 1.0f};
}

} // namespace

void MeshData::GenerateTangents() {
    if (indices.empty()) {
        return;
    }
    TangentMesh tangents{*this, std::vector<Vector4>(indices.size())};
    SMikkTSpaceInterface callbacks{};
    callbacks.m_getNumFaces = [](const SMikkTSpaceContext* context) {
        return static_cast<int>(GetTangentMesh(context).mesh.indices.size() / 3);
    };
    callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
    callbacks.m_getPosition = [](const SMikkTSpaceContext* context, float out[], int face, int corner) {
        const Vector3 position = GetTangentMesh(context).Corner(face, corner).position;
        out[0] = position.x;
        out[1] = position.y;
        out[2] = position.z;
    };
    callbacks.m_getNormal = [](const SMikkTSpaceContext* context, float out[], int face, int corner) {
        const Vector3 normal = GetTangentMesh(context).Corner(face, corner).normal;
        out[0] = normal.x;
        out[1] = normal.y;
        out[2] = normal.z;
    };
    callbacks.m_getTexCoord = [](const SMikkTSpaceContext* context, float out[], int face, int corner) {
        const Vector2 uv = GetTangentMesh(context).Corner(face, corner).uv;
        out[0] = uv.x;
        out[1] = uv.y;
    };
    callbacks.m_setTSpaceBasic = [](const SMikkTSpaceContext* context, const float tangent[], float sign, int face,
                                    int corner) {
        GetTangentMesh(context).cornerTangents[static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(corner)] =
            {tangent[0], tangent[1], tangent[2], sign < 0.0f ? -1.0f : 1.0f};
    };
    SMikkTSpaceContext context{&callbacks, &tangents};
    const bool generated = genTangSpaceDefault(&context) != 0;

    // Each corner gives its vertex its tangent; a vertex already given another one is split.
    std::vector<bool> given(vertices.size(), false);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const std::uint32_t index = indices[i];
        Vector4 tangent = generated ? tangents.cornerTangents[i] : MakeAnyTangent(vertices[index].normal);
        if (LengthSquared(Vector3{tangent.x, tangent.y, tangent.z}) < 1e-12f) {
            tangent = MakeAnyTangent(vertices[index].normal);
        }
        if (!given[index]) {
            vertices[index].tangent = tangent;
            given[index] = true;
            continue;
        }
        const Vector4& existing = vertices[index].tangent;
        const bool same = existing.w == tangent.w && Dot(Vector3{existing.x, existing.y, existing.z},
                                                         Vector3{tangent.x, tangent.y, tangent.z}) > 0.9999f;
        if (!same) {
            MeshVertex split = vertices[index];
            split.tangent = tangent;
            indices[i] = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(split);
            given.push_back(true);
        }
    }
}

void MeshData::RecalculateNormals() {
    for (auto& vertex : vertices) {
        vertex.normal = {};
    }
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        auto& a = vertices[indices[i]];
        auto& b = vertices[indices[i + 1]];
        auto& c = vertices[indices[i + 2]];
        // Not normalized: its length is twice the triangle's area, which weights larger faces more.
        const Vector3 faceNormal = Cross(b.position - a.position, c.position - a.position);
        a.normal += faceNormal;
        b.normal += faceNormal;
        c.normal += faceNormal;
    }
    for (auto& vertex : vertices) {
        vertex.normal = Normalize(vertex.normal);
    }
}

BoundingBox MeshData::GetBoundingBox() const {
    if (vertices.empty()) {
        return {};
    }
    BoundingBox box{vertices.front().position, vertices.front().position};
    for (const auto& vertex : vertices) {
        box.min = {std::min(box.min.x, vertex.position.x), std::min(box.min.y, vertex.position.y),
                   std::min(box.min.z, vertex.position.z)};
        box.max = {std::max(box.max.x, vertex.position.x), std::max(box.max.y, vertex.position.y),
                   std::max(box.max.z, vertex.position.z)};
    }
    return box;
}

BoundingSphere MeshData::GetBounds() const {
    if (vertices.empty()) {
        return {};
    }
    BoundingSphere bounds{.center = GetBoundingBox().GetCenter()};
    for (const auto& vertex : vertices) {
        bounds.radius = std::max(bounds.radius, Distance(bounds.center, vertex.position));
    }
    return bounds;
}

} // namespace Spot
