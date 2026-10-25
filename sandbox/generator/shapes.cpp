#include "shapes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

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

// How the texture maps onto a turned profile's side from a point to the next: around the side's half of the round
// shapes' layout, or onto one of their ends' discs.
enum class Mapping {
    Side,
    TopCap,
    BottomCap,
};

struct TurnedPoint {
    float radius;
    float y;
    bool smooth = false;
    Mapping mapping = Mapping::Side;
};

// Turns the profile, from the top to the bottom, around Y. uv gives each vertex's texture coordinates from the index
// of its point, the mapping of the side it is on, how far around it is, from 0 to 1, and its direction's cosine and
// sine.
template <typename Uv>
MeshData Turn(std::span<const TurnedPoint> profile, std::uint32_t segments, Uv&& uv) {
    MeshData mesh;
    const std::size_t sides = profile.size() - 1;
    // Each side's outward normal, out from the axis and up: going down the profile, outward is to its left.
    std::vector<Vector2> normals(sides);
    for (std::size_t i = 0; i < sides; ++i) {
        const Vector2 along{profile[i + 1].radius - profile[i].radius, profile[i + 1].y - profile[i].y};
        normals[i] = Spot::Normalize(Vector2{-along.y, along.x});
    }
    for (std::size_t i = 0; i < sides; ++i) {
        const auto normalAt = [&](std::size_t point) {
            if (!profile[point].smooth || (point == i && i == 0) || (point == i + 1 && i + 1 >= sides)) {
                return normals[i];
            }
            return Spot::Normalize(normals[i] + normals[point == i ? i - 1 : i + 1]);
        };
        const Vector2 normalA = normalAt(i);
        const Vector2 normalB = normalAt(i + 1);
        const auto vertex = [&](std::size_t point, Vector2 normal, std::uint32_t k) {
            const float angle = Angle(k, segments);
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            return MeshVertex{
                {profile[point].radius * c, profile[point].y, profile[point].radius * s},
                Spot::Normalize(Vector3{normal.x * c, normal.y, normal.x * s}),
                uv(point, profile[i].mapping, static_cast<float>(k) / static_cast<float>(segments), c, s)};
        };
        for (std::uint32_t j = 0; j < segments; ++j) {
            const float middle = (Angle(j, segments) + Angle(j + 1, segments)) * 0.5f;
            const Vector3 facing{normals[i].x * std::cos(middle), normals[i].y, normals[i].x * std::sin(middle)};
            // Sides meeting the axis are fans of triangles.
            if (profile[i].radius <= 0.0f) {
                AddTriangle(mesh, vertex(i, normalA, j), vertex(i + 1, normalB, j + 1), vertex(i + 1, normalB, j),
                            facing);
            } else if (profile[i + 1].radius <= 0.0f) {
                AddTriangle(mesh, vertex(i, normalA, j), vertex(i, normalA, j + 1), vertex(i + 1, normalB, j), facing);
            } else {
                AddQuad(mesh, vertex(i, normalA, j), vertex(i, normalA, j + 1), vertex(i + 1, normalB, j + 1),
                        vertex(i + 1, normalB, j), facing);
            }
        }
    }
    return mesh;
}

// Texture coordinates in the round shapes' layout: the side from sideTop down to sideBottom over the side's half, and
// the ends' discs out to capRadius.
auto RoundLayout(std::span<const TurnedPoint> profile, float sideTop, float sideBottom, float capRadius) {
    return [=](std::size_t point, Mapping mapping, float around, float c, float s) {
        if (mapping == Mapping::Side) {
            return Vector2{around,
                           std::clamp((sideTop - profile[point].y) / (sideTop - sideBottom), 0.0f, 1.0f) * 0.5f};
        }
        const Vector2 center = mapping == Mapping::TopCap ? TopCapCenter : BottomCapCenter;
        return center + Vector2{c, s} * (CapRadius * profile[point].radius / capRadius);
    };
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

MeshData BeveledBox(Vector3 size, float bevel, float uvRepeat) {
    MeshData mesh;
    const Vector3 outer = size * 0.5f;
    bevel = std::min({bevel, outer.x * 0.99f, outer.y * 0.99f, outer.z * 0.99f});
    const Vector3 inner = outer - Vector3{bevel, bevel, bevel};
    // Projected along the normal's main axis, as a box's faces are.
    const auto vertex = [&](Vector3 position, Vector3 normal) {
        const Vector3 a{std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)};
        Vector2 uv = a.x >= a.y && a.x >= a.z ? Vector2{position.z, -position.y}
                     : a.y >= a.z             ? Vector2{position.x, position.z}
                                              : Vector2{position.x, -position.y};
        return MeshVertex{position, normal, uv / uvRepeat};
    };
    const auto point = [](Vector3 extent, Vector3 sign) {
        return Vector3{extent.x * sign.x, extent.y * sign.y, extent.z * sign.z};
    };
    // Every corner of the inner box spreads to three faces, three edges and a corner triangle.
    for (const float x : {-1.0f, 1.0f}) {
        for (const float y : {-1.0f, 1.0f}) {
            for (const float z : {-1.0f, 1.0f}) {
                const Vector3 sign{x, y, z};
                const Vector3 onX{outer.x * x, inner.y * y, inner.z * z};
                const Vector3 onY{inner.x * x, outer.y * y, inner.z * z};
                const Vector3 onZ{inner.x * x, inner.y * y, outer.z * z};
                const Vector3 normal = Spot::Normalize(sign);
                AddTriangle(mesh, vertex(onX, normal), vertex(onY, normal), vertex(onZ, normal), normal);
            }
        }
    }
    const Vector3 axes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    for (int axis = 0; axis < 3; ++axis) {
        const Vector3 u = axes[(axis + 1) % 3];
        const Vector3 v = axes[(axis + 2) % 3];
        for (const float side : {-1.0f, 1.0f}) {
            // The face, inset by the bevel on every side.
            const Vector3 normal = axes[axis] * side;
            const auto corner = [&](float du, float dv) {
                return vertex(point(inner, u * du + v * dv) + point(outer, normal), normal);
            };
            AddQuad(mesh, corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1), normal);
            // The edges between this face and the next axis' faces, once each.
            for (const float other : {-1.0f, 1.0f}) {
                const Vector3 edgeNormal = Spot::Normalize(normal + u * other);
                const auto edge = [&](float dv, bool onFace) {
                    const Vector3 position = onFace ? point(outer, normal) + point(inner, u * other + v * dv)
                                                    : point(inner, normal + v * dv) + point(outer, u * other);
                    return vertex(position, edgeNormal);
                };
                AddQuad(mesh, edge(-1, true), edge(1, true), edge(1, false), edge(-1, false), edgeNormal);
            }
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

MeshData Drum(float radius, float height, std::uint32_t segments) {
    const float top = height * 0.5f;
    // How far the lids sink inside the chimes and how far in from the side they start, how far the chimes roll over,
    // and how far the hoops stand out and half how high they are.
    constexpr float Sink = 0.012f;
    constexpr float Inset = 0.01f;
    constexpr float Roll = 0.004f;
    constexpr float Hoop = 0.008f;
    constexpr float HoopHalf = 0.016f;
    std::vector<TurnedPoint> profile = {{0.0f, top - Sink, false, Mapping::TopCap},
                                        {radius - Inset, top - Sink, false, Mapping::TopCap},
                                        {radius - Inset * 0.3f, top, true, Mapping::TopCap},
                                        {radius, top - Roll, true, Mapping::Side}};
    for (const float hoop : {top - height / 3.0f, top - height * 2.0f / 3.0f}) {
        profile.push_back({radius, hoop + HoopHalf});
        profile.push_back({radius + Hoop, hoop + HoopHalf * 0.35f, true});
        profile.push_back({radius + Hoop, hoop - HoopHalf * 0.35f, true});
        profile.push_back({radius, hoop - HoopHalf});
    }
    profile.push_back({radius, -top + Roll, true, Mapping::BottomCap});
    profile.push_back({radius - Inset * 0.3f, -top, true, Mapping::BottomCap});
    profile.push_back({radius - Inset, -top + Sink, false, Mapping::BottomCap});
    profile.push_back({0.0f, -top + Sink, false, Mapping::BottomCap});
    return Turn(profile, segments, RoundLayout(profile, top - Roll, -top + Roll, radius));
}

MeshData Wheel(float radius, float width, std::uint32_t segments) {
    const float side = width * 0.5f;
    const std::vector<TurnedPoint> profile = {
        {0.0f, side - width * 0.1f, false, Mapping::TopCap},
        {radius * 0.53f, side - width * 0.1f, false, Mapping::TopCap},
        {radius * 0.58f, side - width * 0.02f, false, Mapping::TopCap},
        {radius * 0.76f, side, true, Mapping::TopCap},
        {radius * 0.93f, side - width * 0.03f, true, Mapping::TopCap},
        {radius * 0.985f, side - width * 0.15f, true, Mapping::Side},
        {radius, side - width * 0.3f, true, Mapping::Side},
        {radius, -side + width * 0.3f, true, Mapping::Side},
        {radius * 0.985f, -side + width * 0.15f, true, Mapping::BottomCap},
        {radius * 0.93f, -side + width * 0.03f, true, Mapping::BottomCap},
        {radius * 0.76f, -side, true, Mapping::BottomCap},
        {radius * 0.58f, -side + width * 0.02f, false, Mapping::BottomCap},
        {radius * 0.53f, -side + width * 0.1f, false, Mapping::BottomCap},
        {0.0f, -side + width * 0.1f, false, Mapping::BottomCap},
    };
    MeshData mesh =
        Turn(profile, segments, RoundLayout(profile, side - width * 0.15f, -side + width * 0.15f, radius * 0.985f));
    // Turning a quarter around Z lays the axis along X, and keeps its faces' winding.
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

MeshData TrafficCone(float radius, float height, float plateWidth, float plateHeight, std::uint32_t segments) {
    MeshData mesh = Cone(radius, height, segments);
    MeshData plate = Box({plateWidth, plateHeight, plateWidth}, plateWidth);
    for (MeshVertex& vertex : plate.vertices) {
        vertex.uv = Vector2{0.01f, 0.51f} + vertex.uv * 0.48f;
    }
    Append(mesh, plate, Spot::Matrix4::Translation({0.0f, (plateHeight - height) * 0.5f, 0.0f}));
    return mesh;
}

MeshData Stairs(float width, float stepHeight, float stepDepth, int steps, float uvRepeat) {
    MeshData mesh;
    const float height = stepHeight * static_cast<float>(steps);
    const float length = stepDepth * static_cast<float>(steps);
    // Each step is a block from the ground up to its top, centered on the stairs' bounds like the other shapes.
    for (int i = 0; i < steps; ++i) {
        const float top = stepHeight * static_cast<float>(i + 1);
        MeshData step = Box({width, top, stepDepth}, uvRepeat);
        const Vector3 offset{0.0f, top * 0.5f - height * 0.5f,
                             length * 0.5f - stepDepth * (static_cast<float>(i) + 0.5f)};
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (MeshVertex vertex : step.vertices) {
            vertex.position = vertex.position + offset;
            mesh.vertices.push_back(vertex);
        }
        for (const std::uint32_t index : step.indices) {
            mesh.indices.push_back(first + index);
        }
    }
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

MeshData Icosphere(float radius, int subdivisions, const std::function<float(Vector3)>& displacement) {
    // The icosahedron: three golden rectangles' corners.
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<Vector3> points = {{-1, t, 0},  {1, t, 0},  {-1, -t, 0}, {1, -t, 0}, {0, -1, t},  {0, 1, t},
                                   {0, -1, -t}, {0, 1, -t}, {t, 0, -1},  {t, 0, 1},  {-t, 0, -1}, {-t, 0, 1}};
    for (Vector3& point : points) {
        point = Spot::Normalize(point);
    }
    std::vector<std::uint32_t> triangles = {0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11,
                                            4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3,  9,  4, 3,  4,  2, 3, 2, 6, 3,
                                            6, 8,  3,  8, 9,  4, 9, 5, 2, 4, 11, 6,  2, 10, 8,  6, 7, 9, 8, 1};
    for (int level = 0; level < subdivisions; ++level) {
        // Each edge's midpoint is made once, and shared by the two triangles along it.
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> midpoints;
        const auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
            const auto key = std::minmax(a, b);
            if (const auto found = midpoints.find(key); found != midpoints.end()) {
                return found->second;
            }
            points.push_back(Spot::Normalize(points[a] + points[b]));
            const auto index = static_cast<std::uint32_t>(points.size() - 1);
            midpoints.emplace(key, index);
            return index;
        };
        std::vector<std::uint32_t> finer;
        for (std::size_t i = 0; i < triangles.size(); i += 3) {
            const std::uint32_t a = triangles[i];
            const std::uint32_t b = triangles[i + 1];
            const std::uint32_t c = triangles[i + 2];
            const std::uint32_t ab = midpoint(a, b);
            const std::uint32_t bc = midpoint(b, c);
            const std::uint32_t ca = midpoint(c, a);
            finer.insert(finer.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        triangles = std::move(finer);
    }
    MeshData mesh;
    for (const Vector3& direction : points) {
        const float scale = radius * (1.0f + (displacement ? displacement(direction) : 0.0f));
        // Projected from above; materials on these lumpy shapes usually project from the world instead.
        mesh.vertices.push_back({direction * scale, direction, {0.5f + direction.x * 0.5f, 0.5f - direction.z * 0.5f}});
    }
    // Displaced, the normals are the average of the faces around each vertex, weighted by their area.
    for (MeshVertex& vertex : mesh.vertices) {
        vertex.normal = {};
    }
    for (std::size_t i = 0; i < triangles.size(); i += 3) {
        MeshVertex& a = mesh.vertices[triangles[i]];
        MeshVertex& b = mesh.vertices[triangles[i + 1]];
        MeshVertex& c = mesh.vertices[triangles[i + 2]];
        Vector3 face = Spot::Cross(b.position - a.position, c.position - a.position);
        // Outward, whatever order the table lists the corners in.
        if (Spot::Dot(face, a.position + b.position + c.position) < 0.0f) {
            face = -face;
            std::swap(triangles[i + 1], triangles[i + 2]);
        }
        a.normal = a.normal + face;
        b.normal = b.normal + face;
        c.normal = c.normal + face;
    }
    for (MeshVertex& vertex : mesh.vertices) {
        vertex.normal = Spot::Normalize(vertex.normal);
    }
    mesh.indices = std::move(triangles);
    return mesh;
}

MeshData Frustum(float bottomRadius, float topRadius, float height, std::uint32_t segments) {
    MeshData mesh;
    const float top = height * 0.5f;
    // The sides lean in as much as the radius shrinks over the height.
    const float lean = (bottomRadius - topRadius) / height;
    for (std::uint32_t i = 0; i < segments; ++i) {
        const auto side = [&](std::uint32_t j, bool upper) {
            const float angle = Angle(j, segments);
            const Vector3 around{std::cos(angle), 0.0f, std::sin(angle)};
            const Vector3 normal = Spot::Normalize(Vector3{around.x, lean, around.z});
            const float radius = upper ? topRadius : bottomRadius;
            const float u = static_cast<float>(j) / static_cast<float>(segments);
            return MeshVertex{
                around * radius + Vector3{0.0f, upper ? top : -top, 0.0f}, normal, {u, upper ? 0.0f : 0.5f}};
        };
        const float middle = (Angle(i, segments) + Angle(i + 1, segments)) * 0.5f;
        AddQuad(mesh, side(i, true), side(i + 1, true), side(i + 1, false), side(i, false),
                {std::cos(middle), 0.0f, std::sin(middle)});
    }
    AddCap(mesh, topRadius, top, true, segments);
    AddCap(mesh, bottomRadius, -top, false, segments);
    return mesh;
}

MeshData Extrude(std::span<const ProfilePoint> profile, float length, float uvRepeat) {
    MeshData mesh;
    const float half = length * 0.5f;
    const std::size_t count = profile.size();
    // Each side's outward normal: counterclockwise seen from +X, outward is to the right of the way around.
    std::vector<Vector3> normals(count);
    for (std::size_t i = 0; i < count; ++i) {
        const ProfilePoint& a = profile[i];
        const ProfilePoint& b = profile[(i + 1) % count];
        normals[i] = Spot::Normalize(Vector3{0.0f, -(b.z - a.z), b.y - a.y});
    }
    float around = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const ProfilePoint& a = profile[i];
        const ProfilePoint& b = profile[(i + 1) % count];
        const auto normalAt = [&](std::size_t point, std::size_t sideIndex) {
            if (!profile[point].smooth) {
                return normals[sideIndex];
            }
            const std::size_t before = (point + count - 1) % count;
            return Spot::Normalize(normals[before] + normals[point]);
        };
        const Vector3 na = normalAt(i, i);
        const Vector3 nb = normalAt((i + 1) % count, i);
        const float step = std::sqrt((b.z - a.z) * (b.z - a.z) + (b.y - a.y) * (b.y - a.y));
        const float v0 = around / uvRepeat;
        const float v1 = (around + step) / uvRepeat;
        around += step;
        const float u = length / uvRepeat;
        const MeshVertex corners[] = {{{-half, a.y, a.z}, na, {0.0f, v0}},
                                      {{half, a.y, a.z}, na, {u, v0}},
                                      {{half, b.y, b.z}, nb, {u, v1}},
                                      {{-half, b.y, b.z}, nb, {0.0f, v1}}};
        AddQuad(mesh, corners[0], corners[1], corners[2], corners[3], normals[i]);
    }
    for (const float side : {-1.0f, 1.0f}) {
        const Vector3 normal{side, 0.0f, 0.0f};
        const auto cap = [&](const ProfilePoint& point) {
            return MeshVertex{{side * half, point.y, point.z}, normal, {point.z / uvRepeat, -point.y / uvRepeat}};
        };
        for (std::size_t i = 1; i + 1 < count; ++i) {
            AddTriangle(mesh, cap(profile[0]), cap(profile[i]), cap(profile[i + 1]), normal);
        }
    }
    return mesh;
}

MeshData BeveledExtrude(std::span<const ProfilePoint> profile, float length, float bevel, float uvRepeat) {
    MeshData mesh;
    const float half = length * 0.5f;
    const float inner = half - bevel;
    const std::size_t count = profile.size();
    std::vector<Vector3> normals(count);
    for (std::size_t i = 0; i < count; ++i) {
        const ProfilePoint& a = profile[i];
        const ProfilePoint& b = profile[(i + 1) % count];
        normals[i] = Spot::Normalize(Vector3{0.0f, -(b.z - a.z), b.y - a.y});
    }
    // The profile drawn in by the bevel at the ends: each point moves in along both its sides' normals, as far as
    // keeps both sides the bevel in from where they were.
    std::vector<ProfilePoint> insets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Vector3& before = normals[(i + count - 1) % count];
        const Vector3& after = normals[i];
        const Vector3 miter = (before + after) * (bevel / std::max(1.0f + Spot::Dot(before, after), 0.2f));
        insets[i] = {profile[i].z - miter.z, profile[i].y - miter.y, profile[i].smooth};
    }
    const auto pointNormal = [&](std::size_t point, std::size_t side) {
        if (!profile[point].smooth) {
            return normals[side];
        }
        return Spot::Normalize(normals[(point + count - 1) % count] + normals[point]);
    };
    // Projected along X, as Extrude() maps its caps.
    const auto projected = [&](const ProfilePoint& point) { return Vector2{point.z / uvRepeat, -point.y / uvRepeat}; };
    float around = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t next = (i + 1) % count;
        const ProfilePoint& a = profile[i];
        const ProfilePoint& b = profile[next];
        const Vector3 na = pointNormal(i, i);
        const Vector3 nb = pointNormal(next, i);
        const float step = std::sqrt((b.z - a.z) * (b.z - a.z) + (b.y - a.y) * (b.y - a.y));
        const float v0 = around / uvRepeat;
        const float v1 = (around + step) / uvRepeat;
        around += step;
        const float u0 = (half - inner) / uvRepeat;
        const float u1 = (half + inner) / uvRepeat;
        AddQuad(mesh, {{-inner, a.y, a.z}, na, {u0, v0}}, {{inner, a.y, a.z}, na, {u1, v0}},
                {{inner, b.y, b.z}, nb, {u1, v1}}, {{-inner, b.y, b.z}, nb, {u0, v1}}, normals[i]);
        for (const float end : {-1.0f, 1.0f}) {
            const Vector3 out{end, 0.0f, 0.0f};
            const Vector3 ma = Spot::Normalize(na + out);
            const Vector3 mb = Spot::Normalize(nb + out);
            AddQuad(mesh, {{end * inner, a.y, a.z}, ma, projected(a)}, {{end * inner, b.y, b.z}, mb, projected(b)},
                    {{end * half, insets[next].y, insets[next].z}, mb, projected(insets[next])},
                    {{end * half, insets[i].y, insets[i].z}, ma, projected(insets[i])},
                    Spot::Normalize(normals[i] + out));
        }
    }
    for (const float end : {-1.0f, 1.0f}) {
        const Vector3 normal{end, 0.0f, 0.0f};
        const auto cap = [&](const ProfilePoint& point) {
            return MeshVertex{{end * half, point.y, point.z}, normal, projected(point)};
        };
        for (std::size_t i = 1; i + 1 < count; ++i) {
            AddTriangle(mesh, cap(insets[0]), cap(insets[i]), cap(insets[i + 1]), normal);
        }
    }
    return mesh;
}

MeshData Lathe(std::span<const LathePoint> profile, std::uint32_t segments, float uvRepeat) {
    std::vector<TurnedPoint> turned;
    std::vector<float> along;
    float widest = 0.0f;
    float distance = 0.0f;
    for (std::size_t i = 0; i < profile.size(); ++i) {
        if (i > 0) {
            const float dr = profile[i].radius - profile[i - 1].radius;
            const float dy = profile[i].y - profile[i - 1].y;
            distance += std::sqrt(dr * dr + dy * dy);
        }
        turned.push_back({profile[i].radius, profile[i].y, profile[i].smooth});
        along.push_back(distance);
        widest = std::max(widest, profile[i].radius);
    }
    const float aroundRepeat = 2.0f * Spot::Pi * widest / uvRepeat;
    return Turn(turned, segments, [&](std::size_t point, Mapping, float around, float, float) {
        return Vector2{around * aroundRepeat, along[point] / uvRepeat};
    });
}

void Append(MeshData& to, const MeshData& from, const Spot::Matrix4& transform) {
    const auto first = static_cast<std::uint32_t>(to.vertices.size());
    for (MeshVertex vertex : from.vertices) {
        const Spot::Vector4 position =
            transform * Spot::Vector4{vertex.position.x, vertex.position.y, vertex.position.z, 1.0f};
        const Spot::Vector4 normal = transform * Spot::Vector4{vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0f};
        vertex.position = {position.x, position.y, position.z};
        vertex.normal = Spot::Normalize(Vector3{normal.x, normal.y, normal.z});
        to.vertices.push_back(vertex);
    }
    for (const std::uint32_t index : from.indices) {
        to.indices.push_back(first + index);
    }
}

Spot::MeshData TargetDummy() {
    Spot::MeshData mesh;
    Append(mesh, Box({.8f, .1f, .6f}, .7f), Spot::Matrix4::Translation({0, -.9f, 0}));
    Append(mesh, Box({.08f, 1.2f, .08f}, .7f), Spot::Matrix4::Translation({0, -.3f, 0}));
    Append(mesh, Box({.7f, .72f, .14f}, .7f), Spot::Matrix4::Translation({0, .19f, 0}));
    Append(mesh, Cylinder(.22f, .14f, 24),
           Spot::Matrix4::Translation({0, .73f, 0}) *
               Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({1, 0, 0}, Spot::Pi * .5f)));
    return mesh;
}

} // namespace Generator::Shapes
