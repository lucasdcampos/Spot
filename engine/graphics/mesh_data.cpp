#include "graphics/mesh_data.h"

#include "math/angles.h"

#include <array>
#include <cmath>
#include <cstddef>

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
    return mesh;
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

} // namespace Spot
