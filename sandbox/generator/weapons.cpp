#include "weapons.h"

#include "shapes.h"
#include "textures.h"

#include <spot.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Generator::Weapons {

namespace {

using Shapes::LathePoint;
using Shapes::ProfilePoint;
using Spot::Matrix4;
using Spot::Quaternion;
using Spot::Vector3;
using Textures::Surface;

struct CrossSection {
    float z;
    float y;
    float halfWidth;
    float halfHeight;
};

// A material of one of the surfaces' texture sets, tinted; the textures repeat every 0.25 meters.
GltfMaterial SurfaceMaterial(std::string name, Surface surface, Spot::Color tint = Spot::Color::White) {
    const std::string prefix = "../textures/" + std::string(Textures::GetSurfaceName(surface));
    return {.name = std::move(name),
            .baseColor = tint,
            .baseColorImage = prefix + "_color.png",
            .metallic = 1.0f,
            .roughness = 1.0f,
            .normalImage = prefix + "_normal.png",
            .ormImage = prefix + "_orm.png"};
}

// A plain material that glows, such as sights' dots and screens.
GltfMaterial GlowMaterial(std::string name, Spot::Color color, Spot::Color emissive) {
    return {.name = std::move(name), .baseColor = color, .roughness = 0.4f, .emissive = emissive};
}

constexpr float TextureRepeat = 0.25f;

void Quad(Spot::MeshData& mesh, std::array<Spot::MeshVertex, 4> vertices, Vector3 facing) {
    if (Spot::Dot(Spot::Cross(vertices[1].position - vertices[0].position, vertices[2].position - vertices[0].position),
                  facing) < 0.0f) {
        std::swap(vertices[1], vertices[3]);
    }
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), vertices.begin(), vertices.end());
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

// Flat faces retain their normals; only the small radiused edges blend into their neighbours.
Spot::MeshData RoundedBlock(Vector3 size, float radius) {
    Spot::MeshData mesh;
    const Vector3 half = size * 0.5f;
    radius = std::min({radius, half.x * 0.98f, half.y * 0.98f, half.z * 0.98f});
    const Vector3 core = half - Vector3{radius, radius, radius};
    constexpr int Steps = 3;
    constexpr int Intervals = Steps * 2 + 1;
    const auto coordinate = [radius](int i, float extent) {
        if (i <= Steps) {
            return -extent + radius * static_cast<float>(i) / static_cast<float>(Steps);
        }
        return extent - radius + radius * static_cast<float>(i - Steps - 1) / static_cast<float>(Steps);
    };
    const std::array<Vector3, 3> axes = {Vector3{1, 0, 0}, Vector3{0, 1, 0}, Vector3{0, 0, 1}};
    for (int axis = 0; axis < 3; ++axis) {
        const Vector3 u = axes[(axis + 1) % 3];
        const Vector3 v = axes[(axis + 2) % 3];
        for (const float side : {-1.0f, 1.0f}) {
            const Vector3 facing = axes[axis] * side;
            const auto vertex = [&](int x, int y) {
                const Vector3 cube = facing * Spot::Dot(half, axes[axis]) + u * coordinate(x, Spot::Dot(half, u)) +
                                     v * coordinate(y, Spot::Dot(half, v));
                const Vector3 nearest{std::clamp(cube.x, -core.x, core.x), std::clamp(cube.y, -core.y, core.y),
                                      std::clamp(cube.z, -core.z, core.z)};
                const Vector3 normal = Spot::Normalize(cube - nearest);
                const Vector3 position = nearest + normal * radius;
                return Spot::MeshVertex{
                    position,
                    normal,
                    {Spot::Dot(position, u) / TextureRepeat, -Spot::Dot(position, v) / TextureRepeat}};
            };
            for (int y = 0; y < Intervals; ++y) {
                for (int x = 0; x < Intervals; ++x) {
                    Quad(mesh, {vertex(x, y), vertex(x + 1, y), vertex(x + 1, y + 1), vertex(x, y + 1)}, facing);
                }
            }
        }
    }
    return mesh;
}

Spot::MeshData RoundedProfile(std::span<const ProfilePoint> profile, float width, float radius) {
    // Keep the established side surfaces and cap triangulation, replacing the single bevel with four curved strips.
    Spot::MeshData mesh;
    const auto count = profile.size();
    std::vector<Vector3> normals(count);
    std::vector<Vector3> offsets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& a = profile[i];
        const auto& b = profile[(i + 1) % count];
        normals[i] = Spot::Normalize(Vector3{0, -(b.z - a.z), b.y - a.y});
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Vector3 before = normals[(i + count - 1) % count];
        offsets[i] = (before + normals[i]) * (radius / std::max(1.0f + Spot::Dot(before, normals[i]), 0.2f));
    }
    const float half = width * 0.5f;
    const auto normalAt = [&](std::size_t i, std::size_t edge) {
        return profile[i].smooth ? Spot::Normalize(normals[(i + count - 1) % count] + normals[i]) : normals[edge];
    };
    float around = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        const auto& a = profile[i];
        const auto& b = profile[next];
        const float step = Spot::Length(Vector3{0, b.y - a.y, b.z - a.z});
        const auto sideVertex = [&](std::size_t p, float x, float v) {
            return Spot::MeshVertex{
                {x, profile[p].y, profile[p].z}, normalAt(p, i), {(x + half) / TextureRepeat, v / TextureRepeat}};
        };
        Quad(mesh,
             {sideVertex(i, -half + radius, around), sideVertex(i, half - radius, around),
              sideVertex(next, half - radius, around + step), sideVertex(next, -half + radius, around + step)},
             normals[i]);
        around += step;
        for (const float end : {-1.0f, 1.0f}) {
            const auto bevelVertex = [&](std::size_t p, int ring) {
                const float angle = static_cast<float>(ring) * Spot::Pi / 8.0f;
                const Vector3 outline{0, profile[p].y, profile[p].z};
                const Vector3 position = outline - offsets[p] * (1.0f - std::cos(angle)) +
                                         Vector3{end * (half - radius + radius * std::sin(angle)), 0, 0};
                const Vector3 normal = normalAt(p, i) * std::cos(angle) + Vector3{end * std::sin(angle), 0, 0};
                return Spot::MeshVertex{position, normal, {position.z / TextureRepeat, -position.y / TextureRepeat}};
            };
            for (int ring = 0; ring < 4; ++ring) {
                Quad(mesh,
                     {bevelVertex(i, ring), bevelVertex(next, ring), bevelVertex(next, ring + 1),
                      bevelVertex(i, ring + 1)},
                     Spot::Normalize(normals[i] + Vector3{end, 0, 0}));
            }
        }
    }
    // Reuse the original caps, including their outward winding and projected UVs.
    const auto original = Shapes::BeveledExtrude(profile, width, radius, TextureRepeat);
    for (std::size_t i = 0; i < original.indices.size(); i += 3) {
        const auto& a = original.vertices[original.indices[i]];
        const auto& b = original.vertices[original.indices[i + 1]];
        const auto& c = original.vertices[original.indices[i + 2]];
        if (std::abs(a.normal.x) > 0.999f && std::abs(b.normal.x) > 0.999f && std::abs(c.normal.x) > 0.999f) {
            const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), {a, b, c});
            mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
        }
    }
    return mesh;
}

// An ellipse of smooth points, as the cross-sections of forends and handles are.
std::vector<ProfilePoint> Ellipse(float halfWidth, float halfHeight, int points) {
    std::vector<ProfilePoint> ellipse;
    for (int i = 0; i < points; ++i) {
        const float angle = 2.0f * Spot::Pi * static_cast<float>(i) / static_cast<float>(points);
        ellipse.push_back({std::cos(angle) * halfWidth, std::sin(angle) * halfHeight, true});
    }
    return ellipse;
}

// A model built part by part: each part gathers the shapes of one material. Parts are named after their material, so
// the game finds the moving ones, such as a pistol's "Slide", by name, and the weapons' prefabs find each by its
// index, so parts keep their order.
class Builder {
public:
    explicit Builder(bool roundedEdges = true) : m_roundedEdges(roundedEdges) {}

    std::size_t AddMaterial(GltfMaterial material) {
        m_model.materials.push_back(std::move(material));
        m_model.parts.push_back({.name = m_model.materials.back().name, .mesh = {}, .material = m_model.parts.size()});
        return m_model.parts.size() - 1;
    }

    void Box(std::size_t part, Vector3 size, Vector3 position, float tiltDegrees = 0.0f) {
        Add(part, Shapes::Box(size, TextureRepeat), Place(position, tiltDegrees));
    }

    // Rounded off by the bevel on every edge.
    void Block(std::size_t part, Vector3 size, float bevel, Vector3 position, float tiltDegrees = 0.0f) {
        Add(part, m_roundedEdges ? RoundedBlock(size, bevel) : Shapes::BeveledBox(size, bevel, TextureRepeat),
            Place(position, tiltDegrees));
    }

    // A side view, as Shapes::Extrude() takes profiles, Z toward the back and Y up, as wide as given across X, its
    // edges beveled: the outline of a slide, a grip or a stock.
    void Profile(std::size_t part, std::initializer_list<ProfilePoint> profile, float width, float bevel,
                 Vector3 position, float tiltDegrees = 0.0f) {
        const auto points = std::span(profile.begin(), profile.size());
        Add(part,
            m_roundedEdges ? RoundedProfile(points, width, bevel)
                           : Shapes::BeveledExtrude(points, width, bevel, TextureRepeat),
            Place(position, tiltDegrees));
    }

    // A profile turned around Y in the side view, rolled around the model's Z axis through the point by the angle,
    // as the prongs around a barrel are.
    void RolledProfile(std::size_t part, std::initializer_list<ProfilePoint> profile, float width, float bevel,
                       Vector3 axis, float rollDegrees) {
        Add(part, Shapes::BeveledExtrude(std::span(profile.begin(), profile.size()), width, bevel, TextureRepeat),
            Matrix4::Translation(axis) *
                Matrix4::Rotation(Quaternion::FromAxisAngle({0, 0, 1}, Spot::ToRadians(rollDegrees))) *
                Matrix4::Translation(-axis));
    }

    // A cross-section, X across and Y up, pushed along Z for the length, its ends beveled, as a forend is.
    void Section(std::size_t part, std::span<const ProfilePoint> section, float length, float bevel, Vector3 position) {
        Add(part, RoundedProfile(section, length, bevel),
            Matrix4::Translation(position) * Matrix4::Rotation(Quaternion::FromAxisAngle({0, 1, 0}, Spot::Pi * 0.5f)));
    }

    // A profile turned around Z, its top toward the back (+Z), as barrels, muzzles and rockets are turned; spun around
    // its axis by the angle, so the flats of few segments face where they should.
    void Turned(std::size_t part, std::initializer_list<LathePoint> profile, Vector3 position,
                std::uint32_t segments = 64, float spinDegrees = 0.0f) {
        Add(part, Shapes::Lathe(std::span(profile.begin(), profile.size()), segments, TextureRepeat),
            Matrix4::Translation(position) * AlongZ() *
                Matrix4::Rotation(Quaternion::FromAxisAngle({0, 1, 0}, Spot::ToRadians(spinDegrees))));
    }

    // A cylinder along Z, as barrels and tubes lie.
    void Tube(std::size_t part, float radius, float length, Vector3 position, std::uint32_t segments = 32) {
        Add(part, Shapes::Cylinder(radius, length, segments), Matrix4::Translation(position) * AlongZ());
    }

    void SidePin(std::size_t part, float radius, Vector3 position) {
        const std::array<LathePoint, 6> profile = {{{0, 0.001f},
                                                    {radius * 0.85f, 0.001f},
                                                    {radius, 0.0005f},
                                                    {radius, -0.001f},
                                                    {radius * 0.85f, -0.0015f},
                                                    {0, -0.0015f}}};
        Add(part, Shapes::Lathe(profile, 32, TextureRepeat),
            Matrix4::Translation(position) * Matrix4::Rotation(Quaternion::FromAxisAngle({0, 0, 1}, Spot::Pi * 0.5f)));
    }

    void Stock(std::size_t part, std::initializer_list<CrossSection> sections) {
        Spot::MeshData mesh;
        const std::span rings(sections.begin(), sections.size());
        constexpr int Segments = 48;
        const auto vertex = [&](std::size_t ring, int segment) {
            const float angle = static_cast<float>(segment) * 2.0f * Spot::Pi / static_cast<float>(Segments);
            const auto& section = rings[ring];
            const auto& previous = rings[ring == 0 ? ring : ring - 1];
            const auto& next = rings[std::min(ring + 1, rings.size() - 1)];
            const Vector3 around{-section.halfWidth * std::sin(angle), section.halfHeight * std::cos(angle), 0};
            const Vector3 along{(next.halfWidth - previous.halfWidth) * std::cos(angle),
                                next.y - previous.y + (next.halfHeight - previous.halfHeight) * std::sin(angle),
                                next.z - previous.z};
            const Vector3 position{section.halfWidth * std::cos(angle),
                                   section.y + section.halfHeight * std::sin(angle), section.z};
            return Spot::MeshVertex{position,
                                    Spot::Normalize(Spot::Cross(around, along)),
                                    {section.z / TextureRepeat, static_cast<float>(segment) / Segments * 0.5f}};
        };
        for (std::size_t ring = 0; ring + 1 < rings.size(); ++ring) {
            for (int segment = 0; segment < Segments; ++segment) {
                const auto a = vertex(ring, segment);
                Quad(mesh, {a, vertex(ring, segment + 1), vertex(ring + 1, segment + 1), vertex(ring + 1, segment)},
                     a.normal);
            }
        }
        for (const std::size_t ring : {std::size_t{0}, rings.size() - 1}) {
            const Vector3 normal{0, 0, ring == 0 ? -1.0f : 1.0f};
            for (int segment = 0; segment < Segments; ++segment) {
                auto a = vertex(ring, segment);
                auto b = vertex(ring, segment + 1);
                a.normal = b.normal = normal;
                if (ring == 0) {
                    std::swap(a, b);
                }
                const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.insert(mesh.vertices.end(), {{{0, rings[ring].y, rings[ring].z}, normal, {}}, a, b});
                mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
            }
        }
        Add(part, mesh, Matrix4{});
    }

    // An aluminium free-float handguard with actual openings and interior walls, rather than painted-on slots.
    void Handguard(std::size_t part, Vector3 position) {
        Spot::MeshData mesh;
        constexpr int Segments = 64;
        constexpr int Rows = 13;
        constexpr float Radius = 0.028f;
        constexpr float InnerRadius = 0.0255f;
        const auto open = [](int column, int row) {
            const int wrapped = (column + Segments) % Segments;
            const bool vent =
                wrapped <= 1 || wrapped >= 62 || (wrapped >= 30 && wrapped <= 33) || (wrapped >= 46 && wrapped <= 49);
            return row > 0 && row < Rows - 1 && row % 2 == 1 && vent;
        };
        const auto radial = [](int column) {
            const float angle = static_cast<float>(column) * 2.0f * Spot::Pi / static_cast<float>(Segments);
            return Vector3{std::cos(angle), std::sin(angle), 0};
        };
        const auto zAt = [](int row) { return -0.135f + 0.27f * static_cast<float>(row) / static_cast<float>(Rows); };
        const auto vertex = [&](int column, int row, float radius, Vector3 normal) {
            return Spot::MeshVertex{
                radial(column) * radius + Vector3{0, 0, zAt(row)},
                normal,
                {static_cast<float>(column) / static_cast<float>(Segments) * 2.0f * Spot::Pi * Radius / TextureRepeat,
                 zAt(row) / TextureRepeat}};
        };
        for (int row = 0; row < Rows; ++row) {
            for (int column = 0; column < Segments; ++column) {
                if (open(column, row)) {
                    continue;
                }
                const Vector3 facing = Spot::Normalize(radial(column) + radial(column + 1));
                for (const float radius : {Radius, InnerRadius}) {
                    const float sign = radius == Radius ? 1.0f : -1.0f;
                    Quad(mesh,
                         {vertex(column, row, radius, radial(column) * sign),
                          vertex(column + 1, row, radius, radial(column + 1) * sign),
                          vertex(column + 1, row + 1, radius, radial(column + 1) * sign),
                          vertex(column, row + 1, radius, radial(column) * sign)},
                         facing * sign);
                }
                for (const int edge : {0, 1}) {
                    if ((edge == 0 && (row == 0 || open(column, row - 1))) ||
                        (edge == 1 && (row == Rows - 1 || open(column, row + 1)))) {
                        const Vector3 normal{0, 0, edge == 0 ? -1.0f : 1.0f};
                        Quad(mesh,
                             {vertex(column, row + edge, Radius, normal),
                              vertex(column + 1, row + edge, Radius, normal),
                              vertex(column + 1, row + edge, InnerRadius, normal),
                              vertex(column, row + edge, InnerRadius, normal)},
                             normal);
                    }
                    if (open(column + (edge == 0 ? -1 : 1), row)) {
                        const Vector3 r = radial(column + edge);
                        const Vector3 normal = Vector3{-r.y, r.x, 0} * (edge == 0 ? -1.0f : 1.0f);
                        Quad(mesh,
                             {vertex(column + edge, row, Radius, normal),
                              vertex(column + edge, row + 1, Radius, normal),
                              vertex(column + edge, row + 1, InnerRadius, normal),
                              vertex(column + edge, row, InnerRadius, normal)},
                             normal);
                    }
                }
            }
        }
        Add(part, mesh, Matrix4::Translation(position));
    }

    // Points the game finds by name: "Muzzle", where shots leave, "Eye", where the eye is when aiming down the
    // sights, in line with them, as far behind them as the item is held from the face, and "Hip", the point held where
    // items are held from the hip, the grip at the origin when there is none.
    void Marker(std::string name, Vector3 position) { m_model.markers.push_back({std::move(name), position}); }

    // Without tangents, which the importer makes.
    GltfModel Build() { return std::move(m_model); }

private:
    void Add(std::size_t part, const Spot::MeshData& mesh, const Matrix4& transform) {
        Shapes::Append(m_model.parts[part].mesh, mesh, transform);
    }

    static Matrix4 AlongZ() { return Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, Spot::Pi * 0.5f)); }

    // Tilted about X: positive angles lean the top back, toward +Z.
    static Matrix4 Place(Vector3 position, float tiltDegrees) {
        return Matrix4::Translation(position) *
               Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, Spot::ToRadians(tiltDegrees)));
    }

    GltfModel m_model;
    bool m_roundedEdges;
};

constexpr Spot::Color Gunmetal{0.38f, 0.41f, 0.45f};
constexpr Spot::Color Blackened{0.18f, 0.19f, 0.21f};
constexpr Spot::Color Steel{0.64f, 0.66f, 0.69f};

// A pistol grip's side view around its middle, its front shaped for the fingers and a tang over the hand at the back.
void Grip(Builder& builder, std::size_t part, float width, float height, Vector3 position, float tiltDegrees,
          bool tang) {
    const float h = height * 0.5f;
    const float d = 0.022f;
    if (tang) {
        builder.Profile(part,
                        // From the top of the front, which sees every other point, as the ends' caps need.
                        {{-d * 1.05f, h * 0.96f},
                         {-d * 1.12f, h * 0.38f, true},
                         {-d * 1.04f, -h * 0.2f, true},
                         {-d * 1.1f, -h * 0.68f, true},
                         {-d * 0.95f, -h},
                         {d, -h - 0.002f},
                         {d * 1.08f, 0.0f, true},
                         {d, h * 0.72f},
                         {d * 1.12f, h * 0.84f},
                         {d * 1.55f, h * 0.96f},
                         {d * 1.4f, h * 1.07f},
                         {0.0f, h * 1.07f}},
                        // Less than the others', or it would cut the tang's narrow tip inside out.
                        width, width * 0.13f, position, tiltDegrees);
        return;
    }
    builder.Profile(part,
                    {{-d * 0.9f, -h},
                     {d * 0.9f, -h - 0.002f},
                     {d, 0.0f, true},
                     {d * 0.9f, h * 0.9f},
                     {d * 0.5f, h},
                     {-d * 0.9f, h},
                     {-d * 1.05f, h * 0.4f, true},
                     {-d * 0.98f, -h * 0.2f, true},
                     {-d * 1.05f, -h * 0.68f, true}},
                    width, width * 0.2f, position, tiltDegrees);
}

GltfModel Pistol() {
    Builder builder;
    const std::size_t slide = builder.AddMaterial(SurfaceMaterial("Slide", Surface::Gunmetal, Steel));
    const std::size_t details = builder.AddMaterial(SurfaceMaterial("Slide Details", Surface::Gunmetal, Blackened));
    const std::size_t dots = builder.AddMaterial(GlowMaterial("Slide Dots", {0.3f, 0.9f, 0.4f}, {0.5f, 2.5f, 0.8f}));
    const std::size_t frame = builder.AddMaterial(SurfaceMaterial("Frame", Surface::Polymer));
    const std::size_t barrel = builder.AddMaterial(SurfaceMaterial("Barrel", Surface::Gunmetal, Blackened));
    const std::size_t magazine = builder.AddMaterial(SurfaceMaterial("Magazine", Surface::Gunmetal, Gunmetal));

    // The slide, its nose and back chamfered, with serrations front and back, the barrel's hood in its ejection port
    // and three-dot sights, all of which cycle when it fires.
    builder.Profile(
        slide,
        {{-0.17f, 0.014f}, {0.03f, 0.014f}, {0.03f, 0.04f}, {0.025f, 0.046f}, {-0.148f, 0.046f}, {-0.17f, 0.035f}},
        0.03f, 0.0025f, {});
    for (int i = 0; i < 8; ++i) {
        builder.Block(details, {0.0306f, 0.02f, 0.0016f}, 0.0003f,
                      {0.0f, 0.029f, 0.023f - 0.0034f * static_cast<float>(i)});
    }
    for (int i = 0; i < 5; ++i) {
        builder.Block(details, {0.0306f, 0.016f, 0.0016f}, 0.0003f,
                      {0.0f, 0.027f, -0.115f - 0.0034f * static_cast<float>(i)});
    }
    builder.Block(details, {0.0024f, 0.012f, 0.04f}, 0.0008f, {0.0143f, 0.036f, -0.03f});
    builder.Block(details, {0.021f, 0.001f, 0.037f}, 0.0004f, {0.003f, 0.0463f, -0.030f});
    builder.Block(slide, {0.016f, 0.0012f, 0.030f}, 0.0004f, {0.002f, 0.047f, -0.030f});
    builder.Block(slide, {0.0025f, 0.004f, 0.026f}, 0.0007f, {0.0152f, 0.038f, -0.020f});
    builder.Block(details, {0.004f, 0.007f, 0.006f}, 0.001f, {0.0f, 0.0495f, -0.155f});
    builder.Box(dots, {0.0025f, 0.0025f, 0.0015f}, {0.0f, 0.0505f, -0.1515f});
    builder.Block(details, {0.026f, 0.004f, 0.007f}, 0.001f, {0.0f, 0.048f, 0.02f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Block(details, {0.008f, 0.008f, 0.007f}, 0.0012f, {side * 0.0075f, 0.05f, 0.02f});
        builder.Box(dots, {0.0025f, 0.0025f, 0.0015f}, {side * 0.0075f, 0.0515f, 0.0236f});
    }
    builder.Turned(barrel,
                   {{0.0f, 0.006f},
                    {0.0065f, 0.006f},
                    {0.0065f, -0.006f},
                    {0.0042f, -0.006f},
                    {0.0042f, -0.002f},
                    {0.0f, -0.002f}},
                   {0.0f, 0.032f, -0.171f});

    // The frame: its dust cover with a rail under it, the trigger guard, the grip with its tang, and the levers.
    builder.Profile(frame, {{-0.155f, -0.006f}, {0.022f, -0.006f}, {0.03f, 0.002f}, {0.03f, 0.014f}, {-0.155f, 0.014f}},
                    0.028f, 0.003f, {});
    builder.Block(frame, {0.022f, 0.008f, 0.055f}, 0.0015f, {0.0f, -0.01f, -0.125f});
    for (int i = 0; i < 3; ++i) {
        builder.Box(barrel, {0.0226f, 0.0035f, 0.004f}, {0.0f, -0.0125f, -0.14f + 0.015f * static_cast<float>(i)});
    }
    builder.Block(frame, {0.008f, 0.028f, 0.005f}, 0.0015f, {0.0f, -0.019f, -0.086f}, -18.0f);
    builder.Block(frame, {0.008f, 0.005f, 0.048f}, 0.0015f, {0.0f, -0.031f, -0.062f});
    builder.Block(barrel, {0.005f, 0.018f, 0.006f}, 0.0015f, {0.0f, -0.012f, -0.045f}, 10.0f);
    builder.Block(barrel, {0.003f, 0.004f, 0.02f}, 0.001f, {0.0155f, 0.004f, -0.04f});
    builder.Block(barrel, {0.003f, 0.003f, 0.008f}, 0.001f, {0.0155f, 0.002f, -0.085f});
    Grip(builder, frame, 0.03f, 0.105f, {0.0f, -0.055f, 0.008f}, -16.0f, true);
    for (const float side : {-1.0f, 1.0f}) {
        builder.Block(frame, {0.0028f, 0.066f, 0.029f}, 0.0012f, {side * 0.015f, -0.062f, 0.012f}, -16.0f);
        for (const float y : {-0.035f, -0.085f}) {
            builder.SidePin(barrel, 0.0028f, {side * 0.0168f, y, 0.010f - (y + 0.055f) * 0.28f});
        }
        builder.SidePin(barrel, 0.0022f, {side * 0.0145f, 0.001f, -0.065f});
    }
    builder.Block(barrel, {0.0034f, 0.007f, 0.013f}, 0.001f, {0.016f, -0.019f, -0.002f}, -16.0f);

    // The magazine hides in the grip but for its base plate, until a reload drops it out.
    builder.Block(magazine, {0.022f, 0.1f, 0.034f}, 0.003f, {0.0f, -0.058f, 0.01f}, -16.0f);
    builder.Block(magazine, {0.032f, 0.007f, 0.05f}, 0.002f, {0.0f, -0.1088f, 0.0235f}, -16.0f);

    builder.Marker("Muzzle", {0.0f, 0.032f, -0.178f});
    builder.Marker("Eye", {0.0f, 0.0525f, 0.37f});
    builder.Marker("RightHand", {0.035f, -0.095f, 0.025f});
    builder.Marker("LeftHand", {-0.045f, -0.095f, 0.015f});
    return builder.Build();
}

GltfModel Rifle() {
    Builder builder;
    const std::size_t receiver =
        builder.AddMaterial(SurfaceMaterial("Receiver", Surface::Gunmetal, {0.43f, 0.46f, 0.48f, 1.0f}));
    const std::size_t furniture = builder.AddMaterial(SurfaceMaterial("Furniture", Surface::Polymer));
    const std::size_t barrel = builder.AddMaterial(SurfaceMaterial("Barrel", Surface::Gunmetal, Blackened));
    const std::size_t optic = builder.AddMaterial(SurfaceMaterial("Optic", Surface::Gunmetal, {0.16f, 0.16f, 0.17f}));
    GltfMaterial lens = SurfaceMaterial("Optic Glass", Surface::Glass, {0.55f, 0.75f, 0.85f, 0.22f});
    lens.metallic = 0.0f;
    lens.transparent = true;
    const std::size_t glass = builder.AddMaterial(std::move(lens));
    const std::size_t reticle = builder.AddMaterial(GlowMaterial("Reticle", {1.0f, 0.1f, 0.05f}, {12.0f, 0.4f, 0.2f}));
    const std::size_t magazine =
        builder.AddMaterial(SurfaceMaterial("Magazine", Surface::Gunmetal, {0.42f, 0.37f, 0.27f, 1.0f}));

    // The upper receiver with its rail, forward assist and ejection port, and the lower with its flared magazine well.
    const std::array<ProfilePoint, 8> upper = {{{-0.022f, -0.019f},
                                                {0.022f, -0.019f},
                                                {0.024f, -0.012f},
                                                {0.024f, 0.008f},
                                                {0.015f, 0.022f},
                                                {-0.015f, 0.022f},
                                                {-0.024f, 0.008f},
                                                {-0.024f, -0.012f}}};
    builder.Section(receiver, upper, 0.22f, 0.002f, {0, 0.022f, -0.03f});
    builder.Turned(receiver,
                   {{0, 0.04f}, {0.024f, 0.04f}, {0.027f, 0.035f}, {0.027f, -0.035f}, {0.024f, -0.04f}, {0, -0.04f}},
                   {0, 0.018f, -0.18f});
    builder.Profile(receiver,
                    {{-0.065f, -0.045f},
                     {0.06f, -0.042f},
                     {0.06f, 0.0025f},
                     {-0.14f, 0.0025f},
                     {-0.14f, -0.02f},
                     {-0.158f, -0.045f},
                     {-0.155f, -0.068f},
                     {-0.07f, -0.068f}},
                    0.04f, 0.004f, {});
    builder.Block(receiver, {0.022f, 0.008f, 0.57f}, 0.0015f, {0.0f, 0.049f, -0.205f});
    for (int i = 0; i < 19; ++i) {
        builder.Block(barrel, {0.024f, 0.004f, 0.007f}, 0.001f, {0.0f, 0.054f, 0.06f - 0.03f * static_cast<float>(i)});
    }
    builder.Block(receiver, {0.03f, 0.008f, 0.02f}, 0.002f, {0.0f, 0.04f, 0.09f});
    builder.Turned(receiver, {{0.0f, 0.014f}, {0.007f, 0.014f}, {0.007f, -0.014f}, {0.0f, -0.014f}},
                   {0.025f, 0.028f, 0.035f}, 16);
    builder.Block(receiver, {0.007f, 0.025f, 0.063f}, 0.002f, {0.024f, 0.022f, -0.020f});
    builder.Box(barrel, {0.001f, 0.014f, 0.050f}, {0.0278f, 0.024f, -0.020f});
    builder.Block(receiver, {0.01f, 0.004f, 0.06f}, 0.0012f, {0.0f, -0.06f, -0.02f});
    builder.Block(barrel, {0.005f, 0.02f, 0.006f}, 0.0015f, {0.0f, -0.05f, -0.02f}, 10.0f);

    // Free-float aluminium furniture, with openings through which the barrel remains visible.
    builder.Handguard(receiver, {0.0f, 0.018f, -0.355f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.SidePin(barrel, 0.004f, {side * 0.023f, 0.018f, -0.225f});
        for (const float z : {-0.125f, 0.065f}) {
            builder.SidePin(barrel, 0.0035f, {side * 0.021f, -0.012f, z});
        }
    }
    builder.Turned(barrel, {{0.0f, 0.225f}, {0.0095f, 0.225f}, {0.0085f, -0.225f}, {0.0f, -0.225f}},
                   {0.0f, 0.02f, -0.425f});
    builder.Block(barrel, {0.022f, 0.03f, 0.025f}, 0.002f, {0.0f, 0.016f, -0.505f});
    builder.Turned(barrel,
                   {{0.0f, 0.025f},
                    {0.0105f, 0.025f},
                    {0.0135f, 0.02f},
                    {0.0135f, 0.008f},
                    {0.0122f, 0.006f},
                    {0.0122f, -0.002f},
                    {0.0135f, -0.004f},
                    {0.0135f, -0.02f},
                    {0.012f, -0.025f},
                    {0.0075f, -0.025f},
                    {0.0075f, -0.012f},
                    {0.0f, -0.012f}},
                   {0.0f, 0.02f, -0.675f});

    // The grip, the buffer tube with its castle nut, and the stock with its cheek rest and butt plate.
    Grip(builder, furniture, 0.03f, 0.1f, {0.0f, -0.075f, 0.035f}, -20.0f, false);
    builder.Turned(
        receiver,
        {{0.0f, 0.08f}, {0.015f, 0.08f}, {0.015f, -0.07f}, {0.019f, -0.072f}, {0.019f, -0.08f}, {0.0f, -0.08f}},
        {0.0f, 0.015f, 0.16f}, 24);
    builder.Profile(
        furniture, {{0.17f, -0.005f}, {0.33f, -0.005f}, {0.335f, 0.03f}, {0.31f, 0.04f}, {0.2f, 0.04f}, {0.17f, 0.03f}},
        0.04f, 0.005f, {0.0f, -0.005f, 0.0f});
    builder.Profile(furniture, {{0.17f, -0.030f}, {0.33f, -0.055f}, {0.335f, -0.040f}, {0.19f, -0.012f}}, 0.018f,
                    0.003f, {0.0f, -0.005f, 0.0f});
    builder.Block(furniture, {0.042f, 0.09f, 0.015f}, 0.004f, {0.0f, -0.01f, 0.335f});
    builder.Block(furniture, {0.046f, 0.024f, 0.12f}, 0.006f, {0.0f, 0.031f, 0.258f});
    builder.Block(barrel, {0.044f, 0.084f, 0.008f}, 0.003f, {0.0f, -0.010f, 0.345f});
    builder.Block(receiver, {0.055f, 0.006f, 0.014f}, 0.001f, {0.0f, 0.038f, 0.083f});
    builder.Block(barrel, {0.004f, 0.007f, 0.026f}, 0.0015f, {-0.021f, -0.015f, 0.015f});
    builder.Block(receiver, {0.003f, 0.011f, 0.038f}, 0.001f, {0.023f, 0.006f, -0.020f});

    // A curved magazine, as rifles' are.
    builder.Profile(magazine,
                    {{-0.035f, 0.08f},
                     {-0.037f, 0.0f, true},
                     {-0.045f, -0.08f},
                     {0.025f, -0.08f},
                     {0.033f, 0.0f, true},
                     {0.035f, 0.08f}},
                    0.026f, 0.003f, {0.0f, -0.1f, -0.11f}, 14.0f);
    for (const float side : {-1.0f, 1.0f}) {
        for (int i = 0; i < 3; ++i) {
            builder.Block(magazine, {0.0024f, 0.103f, 0.004f}, 0.0009f,
                          {side * 0.0132f, -0.120f, -0.134f + static_cast<float>(i) * 0.020f}, 14.0f);
        }
    }
    builder.Block(magazine, {0.029f, 0.008f, 0.073f}, 0.002f, {0.0f, -0.182f, -0.130f}, 14.0f);

    // A compact tube red-dot sight, with a recessed coated lens and raised adjustment turrets.
    builder.Block(optic, {0.032f, 0.012f, 0.06f}, 0.002f, {0.0f, 0.059f, -0.06f});
    builder.Turned(optic,
                   {{0.023f, 0.034f},
                    {0.025f, 0.034f},
                    {0.026f, 0.032f},
                    {0.026f, -0.032f},
                    {0.025f, -0.034f},
                    {0.023f, -0.034f},
                    {0.023f, 0.034f}},
                   {0.0f, 0.085f, -0.06f});
    builder.SidePin(optic, 0.008f, {0.031f, 0.085f, -0.054f});
    builder.Block(optic, {0.012f, 0.008f, 0.014f}, 0.002f, {0.026f, 0.085f, -0.054f});
    builder.Block(optic, {0.014f, 0.006f, 0.014f}, 0.002f, {0.0f, 0.112f, -0.054f});
    builder.Tube(glass, 0.0228f, 0.0012f, {0.0f, 0.085f, -0.075f}, 64);
    builder.Box(reticle, {0.0016f, 0.0016f, 0.0005f}, {0.0f, 0.085f, -0.0738f});

    builder.Marker("Muzzle", {0.0f, 0.02f, -0.705f});
    builder.Marker("Eye", {0.0f, 0.085f, 0.17f});
    builder.Marker("RightHand", {0.035f, -0.12f, 0.06f});
    builder.Marker("LeftHand", {-0.035f, -0.07f, -0.25f});
    return builder.Build();
}

GltfModel Shotgun() {
    Builder builder;
    const std::size_t receiver = builder.AddMaterial(SurfaceMaterial("Receiver", Surface::Gunmetal, Gunmetal));
    const std::size_t barrel = builder.AddMaterial(SurfaceMaterial("Barrel", Surface::Gunmetal, Blackened));
    const std::size_t stock = builder.AddMaterial(SurfaceMaterial("Stock", Surface::Walnut));
    const std::size_t pump = builder.AddMaterial(SurfaceMaterial("Pump", Surface::Walnut));
    const std::size_t grooves =
        builder.AddMaterial(SurfaceMaterial("Pump Grooves", Surface::Walnut, {0.3f, 0.3f, 0.3f}));
    const std::size_t rubber = builder.AddMaterial(SurfaceMaterial("Butt Pad", Surface::Rubber));
    GltfMaterial brass = SurfaceMaterial("Bead", Surface::PaintedMetal, {0.95f, 0.78f, 0.4f, 1.0f});
    brass.emissive = {0.25f, 0.18f, 0.06f, 1.0f};
    const std::size_t bead = builder.AddMaterial(std::move(brass));

    // The receiver, its back rounded off, with the ejection port on its side and the loading port under it.
    builder.Profile(receiver,
                    {{-0.14f, -0.03f},
                     {0.075f, -0.03f},
                     {0.08f, -0.01f},
                     {0.08f, 0.016f},
                     {0.065f, 0.028f, true},
                     {0.0f, 0.031f},
                     {-0.14f, 0.03f}},
                    0.046f, 0.005f, {});
    builder.Box(barrel, {0.002f, 0.018f, 0.06f}, {0.0231f, 0.008f, -0.04f});
    builder.Block(receiver, {0.0015f, 0.012f, 0.045f}, 0.0006f, {0.0243f, 0.008f, -0.036f});
    for (const float side : {-1.0f, 1.0f}) {
        for (const float z : {-0.075f, 0.025f}) {
            builder.SidePin(barrel, 0.0035f, {side * 0.0234f, -0.011f, z});
        }
        builder.Block(receiver, {0.003f, 0.006f, 0.22f}, 0.001f, {side * 0.013f, -0.020f, -0.19f});
    }
    builder.Box(barrel, {0.028f, 0.002f, 0.07f}, {0.0f, -0.0305f, -0.03f});
    // The barrel with its rib and bead, and the magazine tube under it with its cap.
    builder.Turned(barrel,
                   {{0.0f, 0.26f},
                    {0.0125f, 0.26f},
                    {0.0125f, -0.258f},
                    {0.0115f, -0.26f},
                    {0.0095f, -0.26f},
                    {0.0095f, -0.25f},
                    {0.0f, -0.25f}},
                   {0.0f, 0.016f, -0.4f});
    builder.Block(barrel, {0.008f, 0.002f, 0.49f}, 0.0006f, {0.0f, 0.034f, -0.4f});
    for (int i = 0; i < 9; ++i) {
        builder.Block(barrel, {0.006f, 0.005f, 0.008f}, 0.0007f,
                      {0.0f, 0.030f, -0.17f - static_cast<float>(i) * 0.057f});
    }
    builder.Turned(
        barrel,
        {{0.0f, 0.21f}, {0.011f, 0.21f}, {0.011f, -0.198f}, {0.0118f, -0.2f}, {0.0118f, -0.21f}, {0.0f, -0.21f}},
        {0.0f, -0.014f, -0.35f});
    builder.Block(receiver, {0.03f, 0.05f, 0.015f}, 0.003f, {0.0f, 0.001f, -0.56f});
    builder.Block(bead, {0.003f, 0.004f, 0.003f}, 0.0014f, {0.0f, 0.037f, -0.645f});

    // The forend the player pumps after each shot, rounded around the magazine tube, with grooves for the fingers.
    const std::vector<ProfilePoint> forend = Ellipse(0.025f, 0.021f, 48);
    builder.Section(pump, forend, 0.17f, 0.008f, {0.0f, -0.014f, -0.33f});
    const std::vector<ProfilePoint> groove = Ellipse(0.0253f, 0.0213f, 48);
    for (int i = 0; i < 11; ++i) {
        builder.Section(grooves, groove, 0.002f, 0.0005f, {0.0f, -0.014f, -0.260f - 0.014f * static_cast<float>(i)});
    }

    // The stock in one piece, from its wrist behind the trigger guard to its butt, which the pad caps.
    builder.Stock(stock, {{0.075f, -0.006f, 0.021f, 0.028f},
                          {0.095f, -0.013f, 0.020f, 0.026f},
                          {0.120f, -0.023f, 0.017f, 0.024f},
                          {0.145f, -0.031f, 0.016f, 0.026f},
                          {0.175f, -0.035f, 0.019f, 0.033f},
                          {0.215f, -0.038f, 0.023f, 0.039f},
                          {0.265f, -0.041f, 0.026f, 0.046f},
                          {0.320f, -0.044f, 0.026f, 0.053f},
                          {0.375f, -0.046f, 0.024f, 0.058f},
                          {0.385f, -0.046f, 0.023f, 0.058f}});
    builder.Block(rubber, {0.046f, 0.121f, 0.02f}, 0.004f, {0.0f, -0.0465f, 0.394f});
    builder.Block(receiver, {0.01f, 0.004f, 0.06f}, 0.0012f, {0.0f, -0.045f, 0.02f});
    builder.Block(receiver, {0.008f, 0.016f, 0.004f}, 0.0012f, {0.0f, -0.037f, -0.008f});
    builder.Block(barrel, {0.005f, 0.02f, 0.006f}, 0.0015f, {0.0f, -0.035f, 0.02f}, 10.0f);

    builder.Marker("Muzzle", {0.0f, 0.016f, -0.665f});
    builder.Marker("Eye", {0.0f, 0.0385f, 0.27f});
    builder.Marker("RightHand", {0.035f, -0.10f, 0.12f});
    builder.Marker("LeftHand", {-0.035f, -0.065f, -0.32f});
    return builder.Build();
}

GltfModel RocketLauncher() {
    Builder builder;
    const std::size_t tube =
        builder.AddMaterial(SurfaceMaterial("Launcher Tube", Surface::PaintedMetal, {0.3f, 0.36f, 0.2f, 1.0f}));
    const std::size_t fittings = builder.AddMaterial(SurfaceMaterial("Launcher Fittings", Surface::Gunmetal, Gunmetal));
    const std::size_t band =
        builder.AddMaterial(SurfaceMaterial("Launcher Band", Surface::PaintedMetal, {0.55f, 0.47f, 0.18f, 1.0f}));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Launcher Grip", Surface::Polymer));
    const std::size_t body = builder.AddMaterial(SurfaceMaterial("Rocket Body", Surface::PaintedMetal, Steel));
    const std::size_t warhead =
        builder.AddMaterial(SurfaceMaterial("Rocket Warhead", Surface::PaintedMetal, {0.24f, 0.28f, 0.16f, 1.0f}));

    // The tube, open at both ends, its muzzle ring in front and the flared venturi behind.
    builder.Turned(tube, {{0.054f, 0.475f}, {0.06f, 0.475f}, {0.06f, -0.475f}, {0.054f, -0.475f}, {0.054f, 0.475f}},
                   {0.0f, 0.05f, -0.1f}, 40);
    builder.Turned(fittings,
                   {{0.06f, 0.025f},
                    {0.068f, 0.025f},
                    {0.07f, 0.02f, true},
                    {0.07f, -0.02f, true},
                    {0.068f, -0.025f},
                    {0.06f, -0.025f},
                    {0.06f, 0.025f}},
                   {0.0f, 0.05f, -0.56f}, 40);
    builder.Turned(fittings,
                   {{0.074f, 0.035f},
                    {0.08f, 0.035f},
                    {0.07f, -0.02f, true},
                    {0.068f, -0.035f},
                    {0.06f, -0.035f},
                    {0.074f, 0.035f}},
                   {0.0f, 0.05f, 0.38f}, 40);
    for (const float z : {-0.46f, 0.3f}) {
        builder.Turned(band,
                       {{0.0598f, 0.02f}, {0.0606f, 0.02f}, {0.0606f, -0.02f}, {0.0598f, -0.02f}, {0.0598f, 0.02f}},
                       {0.0f, 0.05f, z}, 40);
    }
    builder.Block(fittings, {0.03f, 0.025f, 0.06f}, 0.003f, {0.0f, -0.005f, -0.04f});
    Grip(builder, grip, 0.032f, 0.1f, {0.0f, -0.045f, -0.02f}, -15.0f, false);
    Grip(builder, grip, 0.03f, 0.09f, {0.0f, -0.045f, -0.25f}, -5.0f, false);
    builder.Block(fittings, {0.01f, 0.004f, 0.05f}, 0.0012f, {0.0f, -0.035f, -0.065f});
    builder.Block(fittings, {0.008f, 0.026f, 0.004f}, 0.001f, {0.0f, -0.022f, -0.088f});
    builder.Block(fittings, {0.004f, 0.018f, 0.005f}, 0.001f, {0.0f, -0.016f, -0.057f}, 15.0f);
    builder.Block(grip, {0.076f, 0.025f, 0.27f}, 0.009f, {0.0f, 0.104f, 0.10f});
    for (const float side : {-1.0f, 1.0f}) {
        for (const float z : {-0.015f, 0.21f}) {
            builder.SidePin(fittings, 0.0035f, {side * 0.038f, 0.104f, z});
        }
        builder.Block(fittings, {0.006f, 0.018f, 0.027f}, 0.002f, {side * 0.061f, 0.05f, 0.24f});
    }

    // Iron sights on the left of the tube: a post in front and an aperture behind.
    builder.Block(fittings, {0.03f, 0.012f, 0.02f}, 0.002f, {-0.055f, 0.085f, -0.35f});
    builder.Block(fittings, {0.005f, 0.025f, 0.005f}, 0.001f, {-0.07f, 0.1f, -0.35f});
    builder.Block(fittings, {0.03f, 0.012f, 0.02f}, 0.002f, {-0.055f, 0.085f, 0.05f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Block(fittings, {0.026f, 0.005f, 0.005f}, 0.001f, {-0.07f, 0.1125f + side * 0.0105f, 0.05f});
        builder.Block(fittings, {0.005f, 0.026f, 0.005f}, 0.001f, {-0.07f + side * 0.0105f, 0.1125f, 0.05f});
    }

    // The loaded rocket, sticking out of the front until it is fired: its body and its ogive warhead.
    builder.Turned(body, {{0.0f, 0.06f}, {0.03f, 0.06f}, {0.035f, 0.055f}, {0.035f, -0.06f}, {0.0f, -0.06f}},
                   {0.0f, 0.05f, -0.6f});
    builder.Turned(warhead,
                   {{0.0f, 0.09f},
                    {0.04f, 0.09f},
                    {0.045f, 0.085f},
                    {0.045f, 0.03f, true},
                    {0.042f, 0.0f, true},
                    {0.034f, -0.035f, true},
                    {0.022f, -0.065f, true},
                    {0.009f, -0.084f, true},
                    {0.0f, -0.09f}},
                   {0.0f, 0.05f, -0.75f}, 40);

    builder.Marker("Muzzle", {0.0f, 0.05f, -0.59f});
    builder.Marker("Eye", {-0.07f, 0.1125f, 0.34f});
    // Carried on the shoulder, further right and lower than what is held in the hand.
    builder.Marker("Hip", {-0.07f, 0.05f, 0.12f});
    builder.Marker("RightHand", {0.035f, -0.09f, 0.005f});
    builder.Marker("LeftHand", {-0.040f, -0.09f, -0.23f});
    return builder.Build();
}

GltfModel ToolGun() {
    Builder builder;
    const std::size_t body =
        builder.AddMaterial(SurfaceMaterial("Body", Surface::PaintedMetal, {0.85f, 0.85f, 0.88f, 1.0f}));
    const std::size_t trim =
        builder.AddMaterial(SurfaceMaterial("Trim", Surface::Gunmetal, {0.15f, 0.17f, 0.2f, 1.0f}));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Grip", Surface::Polymer));
    const std::size_t glow = builder.AddMaterial(GlowMaterial("Glow", {0.1f, 0.6f, 0.8f}, {0.3f, 2.4f, 3.2f}));
    const std::size_t screen = builder.AddMaterial(GlowMaterial("Screen", {0.05f, 0.08f, 0.1f}, {0.15f, 1.0f, 1.3f}));

    // A body tapering to its nose, a spine along its top and vents along its sides, and an emitter in front.
    builder.Profile(
        body,
        {{-0.18f, -0.012f}, {0.04f, -0.0225f}, {0.04f, 0.045f}, {0.032f, 0.0525f}, {-0.14f, 0.0525f}, {-0.18f, 0.03f}},
        0.055f, 0.01f, {});
    builder.Block(trim, {0.012f, 0.03f, 0.14f}, 0.004f, {0.0f, 0.06f, -0.08f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Box(glow, {0.002f, 0.008f, 0.15f}, {side * 0.0281f, 0.02f, -0.08f});
        for (int i = 0; i < 4; ++i) {
            builder.Block(trim, {0.002f, 0.004f, 0.026f}, 0.0006f,
                          {side * 0.0279f, 0.036f - 0.007f * static_cast<float>(i), -0.01f});
        }
    }
    builder.Turned(trim, {{0.0f, 0.006f}, {0.024f, 0.006f}, {0.026f, 0.004f}, {0.026f, -0.006f}, {0.0f, -0.006f}},
                   {0.0f, 0.015f, -0.186f});
    builder.Turned(trim,
                   {{0.0f, 0.03f},
                    {0.02f, 0.03f},
                    {0.02f, 0.0f},
                    {0.022f, -0.004f},
                    {0.022f, -0.012f},
                    {0.02f, -0.016f},
                    {0.02f, -0.025f},
                    {0.017f, -0.03f},
                    {0.0f, -0.03f}},
                   {0.0f, 0.015f, -0.21f});
    builder.Tube(glow, 0.012f, 0.03f, {0.0f, 0.015f, -0.25f});
    builder.Box(screen, {0.04f, 0.045f, 0.003f}, {0.0f, 0.02f, 0.0405f});
    Grip(builder, grip, 0.034f, 0.1f, {0.0f, -0.07f, 0.0f}, -15.0f, false);

    builder.Marker("Muzzle", {0.0f, 0.015f, -0.266f});
    builder.Marker("Eye", {0.0f, 0.08f, 0.3f});
    builder.Marker("RightHand", {0.035f, -0.10f, 0.015f});
    builder.Marker("LeftHand", {-0.045f, -0.10f, 0.0f});
    return builder.Build();
}

GltfModel RayGun() {
    Builder builder;
    const auto body = builder.AddMaterial(
        {.name = "Ray Gun Enamel", .baseColor = {0.65f, 0.025f, 0.015f, 1}, .metallic = 0.35f, .roughness = 0.22f});
    const auto brass = builder.AddMaterial(
        {.name = "Ray Gun Polished Brass", .baseColor = {0.8f, 0.55f, 0.18f, 1}, .metallic = 1.0f, .roughness = 0.24f});
    const auto grip = builder.AddMaterial(
        {.name = "Ray Gun Clean Grip", .baseColor = {0.035f, 0.045f, 0.04f, 1}, .roughness = 0.55f});
    const auto energy = builder.AddMaterial(GlowMaterial("Ray Gun Core", {0.08f, 0.35f, 0.03f}, {0.4f, 3.0f, 0.12f}));
    builder.Turned(
        body,
        {{0, 0.09f}, {0.045f, 0.075f}, {0.06f, 0.03f, true}, {0.045f, -0.10f, true}, {0.025f, -0.13f}, {0, -0.13f}},
        {0, 0.03f, -0.06f});
    builder.Tube(energy, 0.027f, 0.16f, {0, 0.03f, -0.24f});
    for (int ring = 0; ring < 5; ++ring) {
        const float z = -0.18f - static_cast<float>(ring) * 0.032f;
        builder.Turned(brass,
                       {{0.026f, 0.006f}, {0.046f, 0.006f}, {0.046f, -0.006f}, {0.026f, -0.006f}, {0.026f, 0.006f}},
                       {0, 0.03f, z});
    }
    builder.Turned(body, {{0.023f, 0.015f}, {0.052f, 0.015f}, {0.061f, -0.01f}, {0.023f, -0.022f}, {0.023f, 0.015f}},
                   {0, 0.03f, -0.345f});
    Grip(builder, grip, 0.038f, 0.12f, {0, -0.075f, 0.008f}, -20.0f, false);
    builder.Block(brass, {0.065f, 0.018f, 0.07f}, 0.004f, {0, -0.018f, -0.015f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Block(body, {0.018f, 0.09f, 0.12f}, 0.005f, {side * 0.052f, 0.055f, -0.035f});
        builder.Tube(energy, 0.009f, 0.08f, {side * 0.064f, 0.045f, -0.055f}, 16);
    }
    // Brass seams, a rear cap and a recessed emitter frame the clean enamel shell.
    builder.Turned(brass, {{0, 0.012f}, {0.034f, 0.012f}, {0.044f, 0}, {0.034f, -0.012f}, {0, -0.012f}},
                   {0, 0.03f, 0.018f}, 64);
    builder.Turned(brass, {{0.023f, 0.003f}, {0.049f, 0.003f}, {0.049f, -0.003f}, {0.023f, -0.003f}},
                   {0, 0.03f, -0.36f}, 64);
    builder.Tube(energy, 0.021f, 0.008f, {0, 0.03f, -0.361f}, 48);
    for (const float side : {-1.0f, 1.0f}) {
        builder.Block(brass, {0.004f, 0.012f, 0.10f}, 0.001f, {side * 0.062f, 0.075f, -0.035f});
        for (int rib = 0; rib < 5; ++rib) {
            builder.Block(grip, {0.003f, 0.004f, 0.028f}, 0.001f,
                          {side * 0.020f, -0.055f - static_cast<float>(rib) * 0.015f, 0.008f});
        }
    }
    builder.Marker("Muzzle", {0, 0.03f, -0.37f});
    builder.Marker("Eye", {0, 0.10f, 0.30f});
    builder.Marker("RightHand", {0.035f, -0.11f, 0.015f});
    builder.Marker("LeftHand", {-0.045f, -0.06f, -0.09f});
    auto model = builder.Build();
    constexpr float Scale = 0.75f;
    for (auto& part : model.parts) {
        for (auto& vertex : part.mesh.vertices) {
            vertex.position *= Scale;
        }
    }
    for (auto& marker : model.markers) {
        marker.position *= Scale;
    }
    return model;
}

GltfModel PhysicsGun() {
    Builder builder;
    const std::size_t body =
        builder.AddMaterial(SurfaceMaterial("Body", Surface::PaintedMetal, {0.95f, 0.55f, 0.1f, 1.0f}));
    const std::size_t trim = builder.AddMaterial(SurfaceMaterial("Trim", Surface::Gunmetal, Gunmetal));
    const std::size_t prongs = builder.AddMaterial(SurfaceMaterial("Prongs", Surface::Gunmetal, Blackened));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Grip", Surface::Polymer));
    const std::size_t core = builder.AddMaterial(GlowMaterial("Core", {0.1f, 0.15f, 0.2f}, {0.8f, 2.0f, 3.0f}));

    // The body, rounded at its ends, banded by rings, its back capped and a flared collar in front.
    builder.Turned(body,
                   {{0.0f, 0.13f},
                    {0.036f, 0.13f},
                    {0.042f, 0.12f, true},
                    {0.042f, -0.12f, true},
                    {0.04f, -0.13f},
                    {0.0f, -0.13f}},
                   {0.0f, 0.02f, -0.08f}, 40);
    for (const float z : {-0.13f, -0.05f, 0.01f}) {
        builder.Turned(
            trim, {{0.0418f, 0.004f}, {0.0432f, 0.004f}, {0.0432f, -0.004f}, {0.0418f, -0.004f}, {0.0418f, 0.004f}},
            {0.0f, 0.02f, z}, 40);
    }
    builder.Turned(trim,
                   {{0.0f, 0.022f},
                    {0.025f, 0.02f, true},
                    {0.04f, 0.01f, true},
                    {0.045f, 0.0f},
                    {0.045f, -0.018f},
                    {0.0f, -0.018f}},
                   {0.0f, 0.02f, 0.063f}, 40);
    builder.Turned(trim,
                   {{0.0f, 0.02f},
                    {0.044f, 0.02f},
                    {0.05f, 0.012f},
                    {0.052f, -0.012f},
                    {0.048f, -0.02f},
                    {0.024f, -0.02f},
                    {0.024f, -0.01f},
                    {0.0f, -0.01f}},
                   {0.0f, 0.02f, -0.22f}, 40);
    for (const float side : {-1.0f, 1.0f}) {
        builder.Tube(trim, 0.006f, 0.2f, {side * 0.045f, 0.035f, -0.09f}, 12);
        for (const float z : {-0.17f, -0.01f}) {
            builder.Block(trim, {0.008f, 0.012f, 0.012f}, 0.002f, {side * 0.042f, 0.032f, z});
        }
    }
    builder.Tube(core, 0.022f, 0.12f, {0.0f, 0.02f, -0.29f});
    // Three claws curling in toward the beam, which spin around it.
    for (const float angle : {0.0f, 120.0f, 240.0f}) {
        builder.RolledProfile(prongs,
                              {{-0.23f, 0.068f},
                               {-0.3f, 0.07f, true},
                               {-0.37f, 0.062f, true},
                               {-0.395f, 0.05f},
                               {-0.39f, 0.042f},
                               {-0.34f, 0.05f, true},
                               {-0.28f, 0.054f, true},
                               {-0.23f, 0.052f}},
                              0.012f, 0.003f, {0.0f, 0.02f, 0.0f}, angle);
    }
    Grip(builder, grip, 0.035f, 0.1f, {0.0f, -0.07f, 0.0f}, -15.0f, false);

    builder.Marker("Muzzle", {0.0f, 0.02f, -0.39f});
    builder.Marker("Eye", {0.0f, 0.075f, 0.3f});
    builder.Marker("Hip", {0.0f, 0.0f, 0.08f});
    builder.Marker("RightHand", {0.035f, -0.10f, 0.015f});
    builder.Marker("LeftHand", {-0.055f, -0.07f, -0.14f});
    return builder.Build();
}

} // namespace

GltfModel MakeFirstPersonArms() {
    Builder builder(false);
    for (const auto* side : {"Right", "Left"}) {
        const auto arm = builder.AddMaterial(
            {.name = std::string(side) + " Forearm", .baseColor = {0.68f, 0.44f, 0.29f}, .roughness = 0.85f});
        // Unit length along -Z; the view stretches it between elbow and wrist.
        builder.Turned(arm, {{0, 0}, {0.048f, 0}, {0.045f, -0.15f}, {0.029f, -0.95f}, {0.028f, -1}, {0, -1}}, {}, 12);
        const auto hand = builder.AddMaterial(
            {.name = std::string(side) + " Hand", .baseColor = {0.68f, 0.44f, 0.29f}, .roughness = 0.85f});
        const float sign = std::string_view(side) == "Right" ? 1.0f : -1.0f;
        builder.Block(hand, {0.035f, 0.070f, 0.065f}, 0.009f, {0, 0.033f, -0.019f});
        // Four curled fingers wrap around the grip; the thumb crosses the other side.
        for (int i = 0; i < 4; ++i) {
            const float y = 0.009f + static_cast<float>(i) * 0.016f;
            builder.Block(hand, {0.050f, 0.014f, 0.018f}, 0.005f, {-sign * 0.020f, y, -0.048f});
            builder.Block(hand, {0.016f, 0.014f, 0.030f}, 0.005f, {-sign * 0.041f, y, -0.029f});
        }
        builder.Block(hand, {0.020f, 0.045f, 0.026f}, 0.007f, {-sign * 0.021f, 0.057f, 0.016f}, -25);
    }
    return builder.Build();
}

std::vector<WeaponModel> MakeWeaponModels() {
    std::vector<WeaponModel> models;
    models.push_back({"pistol", Pistol()});
    models.push_back({"rifle", Rifle()});
    models.push_back({"shotgun", Shotgun()});
    models.push_back({"rocket_launcher", RocketLauncher()});
    models.push_back({"tool_gun", ToolGun()});
    models.push_back({"physics_gun", PhysicsGun()});
    models.push_back({"ray_gun", RayGun()});
    return models;
}

} // namespace Generator::Weapons
