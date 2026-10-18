#include "level_models.h"

#include "shapes.h"
#include "texture_painting.h"
#include "trees.h"

#include <spot.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Generator::LevelModels {

namespace {

using Spot::Matrix4;
using Spot::MeshData;
using Spot::Quaternion;
using Spot::Vector3;
using Textures::Surface;

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

// A model built part by part, each part the shapes of one material.
class Builder {
public:
    std::size_t AddMaterial(GltfMaterial material) {
        m_model.materials.push_back(std::move(material));
        m_model.parts.push_back({.name = m_model.materials.back().name, .mesh = {}, .material = m_model.parts.size()});
        return m_model.parts.size() - 1;
    }

    void Add(std::size_t part, const MeshData& mesh, const Matrix4& transform = {}) {
        Shapes::Append(m_model.parts[part].mesh, mesh, transform);
    }

    MeshData& GetMesh(std::size_t part) { return m_model.parts[part].mesh; }

    GltfModel Build() { return std::move(m_model); }

private:
    GltfModel m_model;
};

Matrix4 At(Vector3 position, const Quaternion& rotation = {}) {
    return Matrix4::Translation(position) * Matrix4::Rotation(rotation);
}

Quaternion Turn(Vector3 axis, float degrees) {
    return Quaternion::FromAxisAngle(axis, Spot::ToRadians(degrees));
}

// Stretches a mesh along its axes, turning its normals as the stretch does.
MeshData Scaled(MeshData mesh, Vector3 scale) {
    for (Spot::MeshVertex& vertex : mesh.vertices) {
        vertex.position = {vertex.position.x * scale.x, vertex.position.y * scale.y, vertex.position.z * scale.z};
        vertex.normal =
            Spot::Normalize(Vector3{vertex.normal.x / scale.x, vertex.normal.y / scale.y, vertex.normal.z / scale.z});
    }
    return mesh;
}

// Lumpy noise over a direction, from about -0.5 to 0.5, made of the 2D noise of its three planes.
float Lumps(Vector3 direction, float frequency, std::uint32_t seed) {
    const auto at = [&](float a, float b, std::uint32_t offset) {
        return Textures::Fractal(a * frequency * 0.125f + 0.5f, b * frequency * 0.125f + 0.5f, 8, 8, 4, seed + offset);
    };
    return (at(direction.x, direction.y, 0) + at(direction.y, direction.z, 10) + at(direction.z, direction.x, 20)) /
               3.0f -
           0.5f;
}

constexpr float TerrainExtent = 130.0f;
constexpr float TerrainCell = 2.0f;
constexpr float FieldLevel = -0.03f;
constexpr Vector3 PondCenter = {30.0f, 0.0f, 40.0f};
constexpr float PondRadius = 9.0f;

GltfModel Rock(std::uint32_t seed, Vector3 scale) {
    Builder builder;
    GltfMaterial stone = SurfaceMaterial("Rock", Surface::Rock);
    const std::size_t rock = builder.AddMaterial(std::move(stone));
    MeshData mesh = Shapes::Icosphere(1.0f, 3, [seed](Vector3 direction) {
        // Broad lumps and finer facets, flattened underneath where it rests.
        return Lumps(direction, 2.0f, seed) * 0.7f + Lumps(direction, 6.0f, seed + 100) * 0.18f -
               std::max(-direction.y - 0.4f, 0.0f) * 0.6f;
    });
    builder.Add(rock, Scaled(std::move(mesh), scale));
    return builder.Build();
}

// Cards of leaves or needles, cut out of their texture and seen from both sides.
GltfMaterial FoliageMaterial(std::string name, Surface surface) {
    GltfMaterial material = SurfaceMaterial(std::move(name), surface);
    material.doubleSided = true;
    material.cutout = true;
    return material;
}

GltfModel Tree(const Trees::TreeMeshes& meshes, std::string foliage, Surface surface) {
    Builder builder;
    const std::size_t bark = builder.AddMaterial(SurfaceMaterial("Bark", Surface::Bark));
    const std::size_t leaves = builder.AddMaterial(FoliageMaterial(std::move(foliage), surface));
    builder.Add(bark, meshes.bark);
    builder.Add(leaves, meshes.foliage);
    return builder.Build();
}

GltfModel Oak() {
    return Tree(Trees::Oak(7101), "Leaves", Surface::Leaves);
}

GltfModel Pine() {
    return Tree(Trees::Pine(7201, false), "Needles", Surface::Needles);
}

// A 20-foot container, six meters long along X, with doors at +X.
GltfModel Container() {
    Builder builder;
    const std::size_t walls = builder.AddMaterial(SurfaceMaterial("Container Walls", Surface::Corrugated));
    const std::size_t frame =
        builder.AddMaterial(SurfaceMaterial("Container Frame", Surface::PaintedMetal, {0.25f, 0.26f, 0.28f, 1.0f}));
    constexpr float Length = 6.06f;
    constexpr float Height = 2.59f;
    constexpr float Width = 2.44f;
    constexpr float Post = 0.16f;
    const float halfLength = Length * 0.5f;
    const float halfWidth = Width * 0.5f;
    // The corrugated panels, inset within the frame, the texture's ridges standing upright.
    const float panelLength = Length - Post * 2.0f;
    const float panelHeight = Height - Post * 2.0f;
    for (const float side : {-1.0f, 1.0f}) {
        builder.Add(walls, Shapes::Box({panelLength, panelHeight, 0.05f}, 2.2f),
                    At({0.0f, Height * 0.5f, side * (halfWidth - 0.04f)}));
    }
    builder.Add(walls, Shapes::Box({panelLength, 0.05f, Width - Post}, 2.2f), At({0.0f, Height - 0.06f, 0.0f}));
    builder.Add(walls, Shapes::Box({0.05f, panelHeight, Width - Post * 2.0f}, 2.2f),
                At({-halfLength + 0.06f, Height * 0.5f, 0.0f}));
    // The doors, two leaves with their locking bars.
    for (const float side : {-1.0f, 1.0f}) {
        builder.Add(walls, Shapes::Box({0.06f, panelHeight, halfWidth - Post - 0.01f}, 2.2f),
                    At({halfLength - 0.07f, Height * 0.5f, side * (halfWidth - Post) * 0.5f}));
        for (const float bar : {0.3f, 0.75f}) {
            const float z = side * (halfWidth - Post) * bar;
            builder.Add(frame, Shapes::Cylinder(0.022f, panelHeight - 0.1f, 8),
                        At({halfLength - 0.01f, Height * 0.5f, z}));
            builder.Add(frame, Shapes::BeveledBox({0.05f, 0.08f, 0.12f}, 0.01f, 0.5f),
                        At({halfLength + 0.01f, Height * 0.45f, z + side * 0.06f}));
        }
    }
    builder.Add(walls, Shapes::Box({Length - Post * 2.0f, 0.08f, Width - Post * 2.0f}, 2.2f), At({0.0f, 0.12f, 0.0f}));
    // The frame: corner posts and the rails between them.
    for (const float x : {-1.0f, 1.0f}) {
        for (const float z : {-1.0f, 1.0f}) {
            builder.Add(frame, Shapes::BeveledBox({Post, Height, Post}, 0.015f, 0.5f),
                        At({x * (halfLength - Post * 0.5f), Height * 0.5f, z * (halfWidth - Post * 0.5f)}));
        }
    }
    for (const float y : {Post * 0.5f, Height - Post * 0.5f}) {
        for (const float z : {-1.0f, 1.0f}) {
            builder.Add(frame, Shapes::BeveledBox({Length - Post * 2.0f, Post, Post * 0.8f}, 0.012f, 0.5f),
                        At({0.0f, y, z * (halfWidth - Post * 0.4f)}));
        }
        for (const float x : {-1.0f, 1.0f}) {
            builder.Add(frame, Shapes::BeveledBox({Post * 0.8f, Post, Width - Post * 2.0f}, 0.012f, 0.5f),
                        At({x * (halfLength - Post * 0.4f), y, 0.0f}));
        }
    }
    return builder.Build();
}

GltfModel StreetLamp() {
    Builder builder;
    const std::size_t metal =
        builder.AddMaterial(SurfaceMaterial("Lamp Post", Surface::PaintedMetal, {0.16f, 0.2f, 0.22f, 1.0f}));
    GltfMaterial glow{.name = "Lamp Bulb",
                      .baseColor = {1.0f, 0.95f, 0.85f, 1.0f},
                      .roughness = 0.3f,
                      .emissive = {6.0f, 4.8f, 3.2f, 1.0f}};
    const std::size_t bulb = builder.AddMaterial(std::move(glow));
    builder.Add(metal, Shapes::BeveledBox({0.4f, 0.5f, 0.4f}, 0.04f, 0.5f), At({0.0f, 0.25f, 0.0f}));
    builder.Add(metal, Shapes::Frustum(0.1f, 0.06f, 5.6f, 14), At({0.0f, 3.3f, 0.0f}));
    builder.Add(metal, Shapes::Frustum(0.08f, 0.08f, 0.2f, 14), At({0.0f, 6.15f, 0.0f}));
    // The arm reaching over the road, and the head hanging from its end.
    builder.Add(metal, Shapes::BeveledBox({1.5f, 0.09f, 0.09f}, 0.02f, 0.5f),
                At({0.7f, 6.1f, 0.0f}, Turn({0, 0, 1}, 4.0f)));
    builder.Add(metal, Shapes::BeveledBox({0.7f, 0.14f, 0.34f}, 0.04f, 0.5f), At({1.4f, 6.08f, 0.0f}));
    builder.Add(bulb, Shapes::Box({0.56f, 0.03f, 0.24f}, 0.5f), At({1.4f, 6.0f, 0.0f}));
    return builder.Build();
}

// A concrete barrier, three meters long along X, as roads and ranges are lined with.
GltfModel Barrier() {
    Builder builder;
    const std::size_t concrete = builder.AddMaterial(SurfaceMaterial("Barrier", Surface::Concrete));
    const std::size_t stripes =
        builder.AddMaterial(SurfaceMaterial("Barrier Stripes", Surface::PaintedMetal, {0.95f, 0.7f, 0.1f, 1.0f}));
    const Shapes::ProfilePoint profile[] = {{0.0f, 0.0f},   {0.3f, 0.0f},   {0.3f, 0.08f},
                                            {0.2f, 0.32f},  {0.09f, 0.8f},  {-0.09f, 0.8f},
                                            {-0.2f, 0.32f}, {-0.3f, 0.08f}, {-0.3f, 0.0f}};
    builder.Add(concrete, Shapes::Extrude(profile, 2.96f, 1.0f));
    for (const float side : {-1.0f, 1.0f}) {
        builder.Add(stripes, Shapes::Box({2.6f, 0.08f, 0.01f}, 1.0f),
                    At({0.0f, 0.62f, side * 0.128f}, Turn({1, 0, 0}, side * -14.0f)));
    }
    return builder.Build();
}

GltfModel Bench() {
    Builder builder;
    const std::size_t wood = builder.AddMaterial(SurfaceMaterial("Bench Slats", Surface::Wood));
    const std::size_t metal =
        builder.AddMaterial(SurfaceMaterial("Bench Frame", Surface::PaintedMetal, {0.12f, 0.14f, 0.13f, 1.0f}));
    for (int i = 0; i < 3; ++i) {
        builder.Add(wood, Shapes::BeveledBox({1.8f, 0.04f, 0.12f}, 0.01f, 1.0f),
                    At({0.0f, 0.45f, -0.16f + 0.14f * static_cast<float>(i)}));
    }
    for (int i = 0; i < 2; ++i) {
        builder.Add(wood, Shapes::BeveledBox({1.8f, 0.12f, 0.035f}, 0.01f, 1.0f),
                    At({0.0f, 0.62f + 0.16f * static_cast<float>(i), -0.27f - 0.03f * static_cast<float>(i)},
                       Turn({1, 0, 0}, -12.0f)));
    }
    for (const float x : {-0.75f, 0.75f}) {
        builder.Add(metal, Shapes::BeveledBox({0.05f, 0.45f, 0.05f}, 0.01f, 0.5f), At({x, 0.225f, 0.12f}));
        builder.Add(metal, Shapes::BeveledBox({0.05f, 0.85f, 0.05f}, 0.01f, 0.5f),
                    At({x, 0.42f, -0.24f}, Turn({1, 0, 0}, -10.0f)));
        builder.Add(metal, Shapes::BeveledBox({0.05f, 0.05f, 0.44f}, 0.01f, 0.5f), At({x, 0.41f, -0.02f}));
    }
    return builder.Build();
}

// A ramp curving up from the ground at +Z into a vertical wall three meters high at Z = 0, with a deck behind it.
GltfModel QuarterPipe() {
    Builder builder;
    const std::size_t concrete = builder.AddMaterial(SurfaceMaterial("Quarter Pipe", Surface::Concrete));
    const std::size_t coping =
        builder.AddMaterial(SurfaceMaterial("Coping", Surface::PaintedMetal, {0.7f, 0.72f, 0.75f, 1.0f}));
    constexpr float Radius = 3.0f;
    constexpr int Segments = 18;
    // From the deck's back corner, which sees every other point, forward along the ground, up the curve and back
    // along the deck: counterclockwise with Z to the right and Y up.
    std::vector<Shapes::ProfilePoint> profile;
    profile.push_back({-1.5f, 0.0f});
    for (int i = 0; i <= Segments; ++i) {
        const float angle = Spot::Pi * 0.5f * static_cast<float>(i) / static_cast<float>(Segments);
        profile.push_back(
            {Radius - Radius * std::sin(angle), Radius - Radius * std::cos(angle), i > 0 && i < Segments});
    }
    profile.push_back({-1.5f, Radius});
    builder.Add(concrete, Shapes::Extrude(profile, 6.0f, 2.0f));
    builder.Add(coping, Shapes::Cylinder(0.05f, 6.0f, 12), At({0.0f, Radius, 0.02f}, Turn({0, 0, 1}, 90.0f)));
    return builder.Build();
}

// A chain-link fence panel three meters long along X and two high, between posts.
GltfModel FencePanel() {
    Builder builder;
    const std::size_t posts =
        builder.AddMaterial(SurfaceMaterial("Fence Posts", Surface::PaintedMetal, {0.55f, 0.58f, 0.6f, 1.0f}));
    GltfMaterial link{.name = "Chain Link",
                      .baseColor = {0.7f, 0.72f, 0.74f, 1.0f},
                      .baseColorImage = "../textures/chain_link.png",
                      .metallic = 0.8f,
                      .roughness = 0.45f,
                      .doubleSided = true,
                      .cutout = true};
    const std::size_t mesh = builder.AddMaterial(std::move(link));
    for (const float x : {-1.5f, 1.5f}) {
        builder.Add(posts, Shapes::Cylinder(0.04f, 2.1f, 10), At({x, 1.05f, 0.0f}));
    }
    builder.Add(posts, Shapes::Cylinder(0.025f, 3.0f, 8), At({0.0f, 2.0f, 0.0f}, Turn({0, 0, 1}, 90.0f)));
    builder.Add(mesh, Shapes::Box({2.96f, 1.9f, 0.004f}, 0.25f), At({0.0f, 1.03f, 0.0f}));
    return builder.Build();
}

// Neon lettering: each character is strokes on a grid two units wide and three high.
struct Stroke {
    float x0, y0, x1, y1;
};

std::span<const Stroke> GetStrokes(char character) {
    static constexpr Stroke S[] = {{2, 3, 0, 3}, {0, 3, 0, 1.5f}, {0, 1.5f, 2, 1.5f}, {2, 1.5f, 2, 0}, {2, 0, 0, 0}};
    static constexpr Stroke P[] = {{0, 0, 0, 3}, {0, 3, 2, 3}, {2, 3, 2, 1.5f}, {2, 1.5f, 0, 1.5f}};
    static constexpr Stroke O[] = {{0, 0, 2, 0}, {2, 0, 2, 3}, {2, 3, 0, 3}, {0, 3, 0, 0}};
    static constexpr Stroke T[] = {{0, 3, 2, 3}, {1, 3, 1, 0}};
    static constexpr Stroke E[] = {{2, 3, 0, 3}, {0, 3, 0, 0}, {0, 0, 2, 0}, {0, 1.5f, 1.5f, 1.5f}};
    static constexpr Stroke N[] = {{0, 0, 0, 3}, {0, 3, 2, 0}, {2, 0, 2, 3}};
    static constexpr Stroke G[] = {{2, 3, 0, 3}, {0, 3, 0, 0}, {0, 0, 2, 0}, {2, 0, 2, 1.4f}, {2, 1.4f, 1.1f, 1.4f}};
    static constexpr Stroke I[] = {{1, 0, 1, 3}, {0.4f, 3, 1.6f, 3}, {0.4f, 0, 1.6f, 0}};
    static constexpr Stroke W[] = {{0, 3, 0.5f, 0}, {0.5f, 0, 1, 1.8f}, {1, 1.8f, 1.5f, 0}, {1.5f, 0, 2, 3}};
    static constexpr Stroke R[] = {{0, 0, 0, 3}, {0, 3, 2, 3}, {2, 3, 2, 1.5f}, {2, 1.5f, 0, 1.5f}, {0.8f, 1.5f, 2, 0}};
    static constexpr Stroke K[] = {{0, 0, 0, 3}, {2, 3, 0, 1.3f}, {0.7f, 1.9f, 2, 0}};
    static constexpr Stroke H[] = {{0, 0, 0, 3}, {2, 0, 2, 3}, {0, 1.5f, 2, 1.5f}};
    static constexpr Stroke M[] = {{0, 0, 0, 3}, {0, 3, 1, 1.4f}, {1, 1.4f, 2, 3}, {2, 3, 2, 0}};
    static constexpr Stroke Zero[] = {{0, 0, 2, 0}, {2, 0, 2, 3}, {2, 3, 0, 3}, {0, 3, 0, 0}, {0, 0.4f, 2, 2.6f}};
    static constexpr Stroke One[] = {{1, 0, 1, 3}, {1, 3, 0.4f, 2.4f}};
    static constexpr Stroke Two[] = {{0, 3, 2, 3}, {2, 3, 2, 1.5f}, {2, 1.5f, 0, 1.5f}, {0, 1.5f, 0, 0}, {0, 0, 2, 0}};
    switch (character) {
    case 'S':
        return S;
    case 'P':
        return P;
    case 'O':
        return O;
    case 'T':
        return T;
    case 'E':
        return E;
    case 'N':
        return N;
    case 'G':
        return G;
    case 'I':
        return I;
    case 'W':
        return W;
    case 'R':
        return R;
    case 'K':
        return K;
    case 'H':
        return H;
    case 'M':
        return M;
    case '0':
        return Zero;
    case '1':
        return One;
    case '2':
        return Two;
    default:
        return {};
    }
}

// Text in glowing tubes, centered on the origin in the XY plane and facing +Z, letters as tall as the height.
GltfModel NeonSign(std::string_view text, float height, float thickness) {
    Builder builder;
    const std::size_t tubes = builder.AddMaterial({.name = "Neon",
                                                   .baseColor = {1.0f, 0.5f, 0.15f, 1.0f},
                                                   .roughness = 0.3f,
                                                   .emissive = {8.0f, 3.0f, 0.8f, 1.0f}});
    const float unit = height / 3.0f;
    constexpr float Advance = 3.0f;
    const float width = (static_cast<float>(text.size()) * Advance - 1.0f) * unit;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const float left = -width * 0.5f + static_cast<float>(i) * Advance * unit;
        for (const Stroke& stroke : GetStrokes(text[i])) {
            const float x0 = left + stroke.x0 * unit;
            const float x1 = left + stroke.x1 * unit;
            const float y0 = stroke.y0 * unit - height * 0.5f;
            const float y1 = stroke.y1 * unit - height * 0.5f;
            const float length = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + thickness;
            builder.Add(tubes, Shapes::Box({length, thickness, thickness}, 1.0f),
                        At({(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.0f},
                           Quaternion::FromAxisAngle({0, 0, 1}, std::atan2(y1 - y0, x1 - x0))));
        }
    }
    return builder.Build();
}

// The road's paint, in place: dashes down the middle, a line along each edge and a crossing to the plaza.
GltfModel RoadMarkings() {
    Builder builder;
    const std::size_t paint =
        builder.AddMaterial({.name = "Road Paint", .baseColor = {0.92f, 0.92f, 0.88f, 1.0f}, .roughness = 0.55f});
    for (int x = -60; x <= 60; x += 6) {
        if (std::abs(x) > 4) {
            builder.Add(paint, Shapes::Box({2.6f, 0.006f, 0.16f}, 1.0f), At({static_cast<float>(x), 0.003f, 26.0f}));
        }
    }
    for (const float z : {22.35f, 29.65f}) {
        builder.Add(paint, Shapes::Box({123.0f, 0.006f, 0.14f}, 1.0f), At({0.0f, 0.003f, z}));
    }
    for (int i = 0; i < 6; ++i) {
        builder.Add(paint, Shapes::Box({0.5f, 0.006f, 7.0f}, 1.0f),
                    At({-2.25f + 0.9f * static_cast<float>(i), 0.003f, 26.0f}));
    }
    return builder.Build();
}

// How far a point is outside the level's flat ground, where the hills begin.
float GetDistanceOutside(float x, float z) {
    const float dx = std::max(std::abs(x) - 50.0f, 0.0f);
    const float dz = std::max(std::abs(z) - 46.0f, 0.0f);
    return std::sqrt(dx * dx + dz * dz) - 12.0f;
}

// The pines and boulders scattered over the hills, merged into one mesh per material so the whole forest draws as a
// handful of meshes. Trunks and rocks collide by their triangles.
GltfModel Forest() {
    Builder builder;
    const std::size_t bark = builder.AddMaterial(SurfaceMaterial("Bark", Surface::Bark));
    const std::size_t needles = builder.AddMaterial(FoliageMaterial("Needles", Surface::Needles));
    const std::size_t stone = builder.AddMaterial(SurfaceMaterial("Rock", Surface::Rock));
    // A few pines of their own, sparser than the one placed alone, as the forest is seen from afar.
    const Trees::TreeMeshes kinds[] = {Trees::Pine(7301, true), Trees::Pine(7302, true), Trees::Pine(7303, true)};
    const MeshData rocks[] = {Rock(4001, {1.3f, 0.95f, 1.1f}).parts[0].mesh,
                              Rock(4002, {2.2f, 0.7f, 1.6f}).parts[0].mesh,
                              Rock(4003, {0.9f, 1.9f, 1.0f}).parts[0].mesh};
    const auto random = [](int i, std::uint32_t seed) { return Textures::Random(i, 0, seed); };
    const auto place = [&](std::size_t part, const MeshData& mesh, Vector3 position, float yaw, float scale) {
        MeshData scaled = mesh;
        for (Spot::MeshVertex& vertex : scaled.vertices) {
            vertex.position = vertex.position * scale;
        }
        builder.Add(part, scaled, At(position, Turn({0, 1, 0}, yaw)));
    };
    int pines = 0;
    for (int i = 0; i < 4000 && pines < 70; ++i) {
        const float x = -120.0f + random(i, 5001) * 240.0f;
        const float z = -115.0f + random(i, 5002) * 230.0f;
        const float outside = GetDistanceOutside(x, z);
        if (outside < 2.0f || outside > 55.0f) {
            continue;
        }
        const float scale = 0.9f + random(i, 5003) * 0.6f;
        const Vector3 base{x, GetTerrainHeight(x, z) - 0.4f, z};
        const float yaw = random(i, 5004) * 360.0f;
        const Trees::TreeMeshes& pine = kinds[static_cast<std::size_t>(random(i, 5005) * 2.999f)];
        place(bark, pine.bark, base, yaw, scale);
        place(needles, pine.foliage, base, yaw, scale);
        ++pines;
    }
    int boulders = 0;
    for (int i = 0; i < 4000 && boulders < 26; ++i) {
        const float x = -110.0f + random(i, 6001) * 220.0f;
        const float z = -105.0f + random(i, 6002) * 210.0f;
        const float outside = GetDistanceOutside(x, z);
        if (outside <= 0.0f || outside >= 45.0f) {
            continue;
        }
        const float scale = 1.0f + random(i, 6003) * 1.6f;
        const auto kind = static_cast<std::size_t>(random(i, 6004) * 2.999f);
        place(stone, rocks[kind], {x, GetTerrainHeight(x, z) - 0.3f * scale, z}, random(i, 6005) * 360.0f, scale);
        ++boulders;
    }
    return builder.Build();
}

} // namespace

float GetTerrainHeight(float x, float z) {
    // Outside the level's rounded rectangle, the ground rises into hills, steeper and higher farther out.
    const float dx = std::max(std::abs(x) - 50.0f, 0.0f);
    const float dz = std::max(std::abs(z) - 46.0f, 0.0f);
    const float outside = std::max(std::sqrt(dx * dx + dz * dz) - 12.0f, 0.0f);
    const float u = x / (TerrainExtent * 2.0f) + 0.5f;
    const float v = z / (TerrainExtent * 2.0f) + 0.5f;
    const float broad = Textures::Fractal(u, v, 6, 6, 4, 3001);
    const float ridges = Textures::Fractal(u, v, 16, 16, 3, 3002);
    const float hills = Textures::Smooth(0.0f, 30.0f, outside) * (6.0f + broad * 14.0f) +
                        Textures::Smooth(20.0f, 70.0f, outside) * (4.0f + ridges * 8.0f) +
                        Textures::Smooth(0.0f, 8.0f, outside) * ridges * 1.5f;
    // The pond's hollow, sloping gently to its deepest in the middle.
    const float pond = std::sqrt((x - PondCenter.x) * (x - PondCenter.x) + (z - PondCenter.z) * (z - PondCenter.z));
    const float hollow = Textures::Smooth(PondRadius + 2.0f, PondRadius * 0.3f, pond) * 1.5f;
    return FieldLevel + hills - hollow;
}

GltfModel Terrain() {
    Builder builder;
    const std::size_t grass = builder.AddMaterial(SurfaceMaterial("Grass", Surface::Grass));
    const std::size_t rock = builder.AddMaterial(SurfaceMaterial("Rock", Surface::Rock));
    const int cells = static_cast<int>(TerrainExtent * 2.0f / TerrainCell);
    const auto point = [](int i, int j) {
        const float x = -TerrainExtent + static_cast<float>(i) * TerrainCell;
        const float z = -TerrainExtent + static_cast<float>(j) * TerrainCell;
        return Vector3{x, GetTerrainHeight(x, z), z};
    };
    const auto normal = [&](int i, int j) {
        const Vector3 dx = point(i + 1, j) - point(i - 1, j);
        const Vector3 dz = point(i, j + 1) - point(i, j - 1);
        return Spot::Normalize(Spot::Cross(dz, dx));
    };
    // Each triangle goes to the grass or the rock by how steep it is; each part shares its own vertices.
    std::vector<std::uint32_t> shared[2] = {std::vector<std::uint32_t>((cells + 1) * (cells + 1), UINT32_MAX),
                                            std::vector<std::uint32_t>((cells + 1) * (cells + 1), UINT32_MAX)};
    const auto vertex = [&](std::size_t part, int i, int j) {
        std::uint32_t& index = shared[part == grass ? 0 : 1][static_cast<std::size_t>(j * (cells + 1) + i)];
        MeshData& mesh = builder.GetMesh(part);
        if (index == UINT32_MAX) {
            const Vector3 position = point(i, j);
            index = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({position, normal(i, j), {position.x / 4.0f, position.z / 4.0f}});
        }
        return index;
    };
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const int corners[4][2] = {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}};
            const int triangles[2][3] = {{0, 2, 1}, {0, 3, 2}};
            for (const auto& triangle : triangles) {
                Vector3 positions[3];
                for (int k = 0; k < 3; ++k) {
                    positions[k] = point(corners[triangle[k]][0], corners[triangle[k]][1]);
                }
                const Vector3 face =
                    Spot::Normalize(Spot::Cross(positions[1] - positions[0], positions[2] - positions[0]));
                const std::size_t part = face.y < 0.8f ? rock : grass;
                for (int k = 0; k < 3; ++k) {
                    builder.GetMesh(part).indices.push_back(
                        vertex(part, corners[triangle[k]][0], corners[triangle[k]][1]));
                }
            }
        }
    }
    return builder.Build();
}

std::vector<LevelModel> MakeLevelModels() {
    std::vector<LevelModel> models;
    models.push_back({"terrain", Terrain()});
    models.push_back({"rock_boulder", Rock(4001, {1.3f, 0.95f, 1.1f})});
    models.push_back({"rock_slab", Rock(4002, {2.2f, 0.7f, 1.6f})});
    models.push_back({"rock_spire", Rock(4003, {0.9f, 1.9f, 1.0f})});
    models.push_back({"tree_oak", Oak()});
    models.push_back({"tree_pine", Pine()});
    models.push_back({"container", Container()});
    models.push_back({"street_lamp", StreetLamp()});
    models.push_back({"barrier", Barrier()});
    models.push_back({"bench", Bench()});
    models.push_back({"quarter_pipe", QuarterPipe()});
    models.push_back({"fence_panel", FencePanel()});
    models.push_back({"forest", Forest()});
    models.push_back({"road_markings", RoadMarkings()});
    models.push_back({"sign_spot_engine", NeonSign("SPOT ENGINE", 0.62f, 0.05f)});
    models.push_back({"sign_workshop", NeonSign("WORKSHOP", 0.5f, 0.05f)});
    models.push_back({"sign_10m", NeonSign("10M", 0.9f, 0.09f)});
    models.push_back({"sign_20m", NeonSign("20M", 0.9f, 0.09f)});
    return models;
}

} // namespace Generator::LevelModels
