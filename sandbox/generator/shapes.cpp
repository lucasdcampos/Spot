#include "shapes.h"

#include <cmath>
#include <utility>

namespace Generator::Shapes {

namespace {

using Spot::MeshData;
using Spot::MeshVertex;
using Spot::Vector2;
using Spot::Vector3;

constexpr float CapRadius = 0.24f;
constexpr Vector2 TopCapCenter = {0.25f, 0.75f};
constexpr Vector2 BottomCapCenter = {0.75f, 0.75f};

// Adds the triangle facing the side the normal points to, whatever order its vertices come in.
void AddTriangle(MeshData& mesh, MeshVertex a, MeshVertex b, MeshVertex c, Vector3 facing) {
    if (Spot::Dot(Spot::Cross(b.position - a.position, c.position - a.position), facing) < 0.0f) {
        std::swap(b, c);
    }
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {a, b, c});
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
}

// The corners go around the quad, in either direction.
void AddQuad(MeshData& mesh, MeshVertex a, MeshVertex b, MeshVertex c, MeshVertex d, Vector3 facing) {
    if (Spot::Dot(Spot::Cross(b.position - a.position, c.position - a.position), facing) < 0.0f) {
        std::swap(b, d);
    }
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {a, b, c, d});
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

float Angle(std::uint32_t i, std::uint32_t segments) {
    return 2.0f * Spot::Pi * static_cast<float>(i) / static_cast<float>(segments);
}

// A disc of the round shapes' layout, at the height, facing up or down.
void AddCap(MeshData& mesh, float radius, float y, bool top, std::uint32_t segments) {
    const Vector3 normal{0.0f, top ? 1.0f : -1.0f, 0.0f};
    const Vector2 center = top ? TopCapCenter : BottomCapCenter;
    const MeshVertex middle{{0.0f, y, 0.0f}, normal, center};
    for (std::uint32_t i = 0; i < segments; ++i) {
        const auto rim = [&](std::uint32_t j) {
            const float angle = Angle(j, segments);
            return MeshVertex{{radius * std::cos(angle), y, radius * std::sin(angle)},
                              normal,
                              center + Vector2{std::cos(angle), std::sin(angle)} * CapRadius};
        };
        AddTriangle(mesh, middle, rim(i), rim(i + 1), normal);
    }
}

} // namespace

MeshData Box(Vector3 size, float uvRepeat) {
    MeshData mesh;
    const Vector3 half = size * 0.5f;
    const Vector3 axes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const float halves[3] = {half.x, half.y, half.z};
    for (int axis = 0; axis < 3; ++axis) {
        // The texture's X runs along the box's X, or along Z on the faces at the ends of X; its Y goes down.
        const int u = axis == 0 ? 2 : 0;
        const int v = axis == 1 ? 2 : 1;
        for (const float side : {-1.0f, 1.0f}) {
            const Vector3 normal = axes[axis] * side;
            const auto corner = [&](float du, float dv) {
                const Vector3 position =
                    normal * halves[axis] + axes[u] * (du * halves[u]) + axes[v] * (dv * halves[v]);
                const Vector2 uv{(du + 1.0f) * halves[u] / uvRepeat, (1.0f - dv) * halves[v] / uvRepeat};
                return MeshVertex{position, normal, uv};
            };
            AddQuad(mesh, corner(-1.0f, -1.0f), corner(1.0f, -1.0f), corner(1.0f, 1.0f), corner(-1.0f, 1.0f), normal);
        }
    }
    return mesh;
}

MeshData Cylinder(float radius, float height, std::uint32_t segments) {
    MeshData mesh;
    const float top = height * 0.5f;
    for (std::uint32_t i = 0; i < segments; ++i) {
        const auto side = [&](std::uint32_t j, float y) {
            const float angle = Angle(j, segments);
            const Vector3 normal{std::cos(angle), 0.0f, std::sin(angle)};
            const float u = static_cast<float>(j) / static_cast<float>(segments);
            return MeshVertex{normal * radius + Vector3{0.0f, y, 0.0f}, normal, {u, y > 0.0f ? 0.0f : 0.5f}};
        };
        const float middle = (Angle(i, segments) + Angle(i + 1, segments)) * 0.5f;
        AddQuad(mesh, side(i, top), side(i + 1, top), side(i + 1, -top), side(i, -top),
                {std::cos(middle), 0.0f, std::sin(middle)});
    }
    AddCap(mesh, radius, top, true, segments);
    AddCap(mesh, radius, -top, false, segments);
    return mesh;
}

MeshData Wheel(float radius, float width, std::uint32_t segments) {
    MeshData mesh = Cylinder(radius, width, segments);
    // Turning a quarter around Z lays the cylinder's axis along X, and keeps its faces' winding.
    const auto turn = [](Vector3 v) { return Vector3{v.y, -v.x, v.z}; };
    for (MeshVertex& vertex : mesh.vertices) {
        vertex.position = turn(vertex.position);
        vertex.normal = turn(vertex.normal);
    }
    return mesh;
}

MeshData Cone(float radius, float height, std::uint32_t segments) {
    MeshData mesh;
    const float top = height * 0.5f;
    const auto slantNormal = [&](float angle) {
        return Spot::Normalize(Vector3{height * std::cos(angle), radius, height * std::sin(angle)});
    };
    for (std::uint32_t i = 0; i < segments; ++i) {
        const auto rim = [&](std::uint32_t j) {
            const float angle = Angle(j, segments);
            return MeshVertex{{radius * std::cos(angle), -top, radius * std::sin(angle)},
                              slantNormal(angle),
                              {static_cast<float>(j) / static_cast<float>(segments), 0.5f}};
        };
        const float middle = (Angle(i, segments) + Angle(i + 1, segments)) * 0.5f;
        const MeshVertex tip{{0.0f, top, 0.0f},
                             slantNormal(middle),
                             {(static_cast<float>(i) + 0.5f) / static_cast<float>(segments), 0.0f}};
        AddTriangle(mesh, tip, rim(i), rim(i + 1), slantNormal(middle));
    }
    AddCap(mesh, radius, -top, false, segments);
    return mesh;
}

MeshData Ramp(float width, float height, float length, float uvRepeat) {
    MeshData mesh;
    const float x = width * 0.5f;
    const float y = height * 0.5f;
    const float z = length * 0.5f;
    const auto vertex = [](Vector3 position, Vector3 normal, float u, float v) {
        return MeshVertex{position, normal, {u, v}};
    };
    const float u0 = 0.0f;
    const float u1 = width / uvRepeat;

    const Vector3 down{0.0f, -1.0f, 0.0f};
    AddQuad(mesh, vertex({-x, -y, -z}, down, u0, 0.0f), vertex({x, -y, -z}, down, u1, 0.0f),
            vertex({x, -y, z}, down, u1, length / uvRepeat), vertex({-x, -y, z}, down, u0, length / uvRepeat), down);

    const Vector3 back{0.0f, 0.0f, -1.0f};
    AddQuad(mesh, vertex({-x, y, -z}, back, u0, 0.0f), vertex({x, y, -z}, back, u1, 0.0f),
            vertex({x, -y, -z}, back, u1, height / uvRepeat), vertex({-x, -y, -z}, back, u0, height / uvRepeat), back);

    const Vector3 slope = Spot::Normalize(Vector3{0.0f, length, height});
    const float slopeLength = std::sqrt(length * length + height * height) / uvRepeat;
    AddQuad(mesh, vertex({-x, y, -z}, slope, u0, 0.0f), vertex({x, y, -z}, slope, u1, 0.0f),
            vertex({x, -y, z}, slope, u1, slopeLength), vertex({-x, -y, z}, slope, u0, slopeLength), slope);

    for (const float side : {-1.0f, 1.0f}) {
        const Vector3 normal{side, 0.0f, 0.0f};
        AddTriangle(mesh, vertex({side * x, -y, -z}, normal, 0.0f, height / uvRepeat),
                    vertex({side * x, y, -z}, normal, 0.0f, 0.0f),
                    vertex({side * x, -y, z}, normal, length / uvRepeat, height / uvRepeat), normal);
    }
    return mesh;
}

} // namespace Generator::Shapes
