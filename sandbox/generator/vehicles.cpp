#include "vehicles.h"

#include "shapes.h"
#include "textures.h"

#include <spot.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Generator::Vehicles {

namespace {

using Shapes::LathePoint;
using Shapes::ProfilePoint;
using Spot::Matrix4;
using Spot::Quaternion;
using Spot::Vector3;
using Textures::Surface;

constexpr float TextureRepeat = 0.5f;

GltfMaterial SurfaceMaterial(std::string name, Surface surface, Spot::Color tint, float roughness = 1.0f) {
    const std::string prefix = "../textures/" + std::string(Textures::GetSurfaceName(surface));
    return {.name = std::move(name),
            .baseColor = tint,
            .baseColorImage = prefix + "_color.png",
            .metallic = 1.0f,
            .roughness = roughness,
            .normalImage = prefix + "_normal.png",
            .ormImage = prefix + "_orm.png"};
}

// A part per material, named after it, gathering its shapes; the car's prefab finds each part by its index.
class Builder {
public:
    std::size_t AddMaterial(GltfMaterial material) {
        m_model.materials.push_back(std::move(material));
        m_model.parts.push_back({.name = m_model.materials.back().name, .mesh = {}, .material = m_model.parts.size()});
        return m_model.parts.size() - 1;
    }

    void Add(std::size_t part, const Spot::MeshData& mesh, const Matrix4& transform = Matrix4::Identity()) {
        Shapes::Append(m_model.parts[part].mesh, mesh, transform);
    }

    // Rounded off by the bevel on every edge, tilted about X: positive angles lean its top back, toward +Z.
    void Block(std::size_t part, Vector3 size, float bevel, Vector3 position, float tiltDegrees = 0.0f) {
        Add(part, bevel > 0.0f ? Shapes::BeveledBox(size, bevel, TextureRepeat) : Shapes::Box(size, TextureRepeat),
            Matrix4::Translation(position) *
                Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, Spot::ToRadians(tiltDegrees))));
    }

    // A bar from one point to the other, as wide across X and as thick as given, as a pillar between windows is.
    void Strut(std::size_t part, Vector3 from, Vector3 to, float width, float thickness) {
        const Vector3 along = to - from;
        Add(part, Shapes::Box({width, thickness, Spot::Length(along)}, TextureRepeat),
            Matrix4::Translation((from + to) * 0.5f) * Matrix4::Rotation(Quaternion::LookRotation(along)));
    }

    // A side view pushed across X for the width, centered on X, as the body's and the windows' outlines are.
    void Side(std::size_t part, std::span<const ProfilePoint> profile, float width, float bevel = 0.0f) {
        Add(part, bevel > 0.0f ? Shapes::BeveledExtrude(profile, width, bevel, TextureRepeat)
                               : Shapes::Extrude(profile, width, TextureRepeat));
    }

    GltfModel Build() { return std::move(m_model); }

private:
    GltfModel m_model;
};

std::vector<ProfilePoint> WheelArches(float nose, float bottom, float radius, std::span<const float> axles) {
    std::vector<ProfilePoint> outline = {{nose, bottom}};
    for (const float axle : axles) {
        // Over the wheel from its front to its rear, rounding the arch's inside.
        constexpr int Segments = 12;
        for (int i = 0; i <= Segments; ++i) {
            const float angle = Spot::Pi * (1.0f - static_cast<float>(i) / Segments);
            outline.push_back(
                {axle + radius * std::cos(angle), bottom + radius * std::sin(angle), i > 0 && i < Segments});
        }
    }
    return outline;
}

// Counterclockwise from the nose's bottom, Z to the rear.
std::vector<ProfilePoint> BodyOutline() {
    constexpr float Bottom = 0.3f;
    constexpr float Axles[] = {-1.3f, 1.3f};
    auto outline = WheelArches(-2.02f, Bottom, 0.42f, Axles);
    outline.insert(outline.end(), {{2.04f, Bottom},
                                   {2.1f, 0.38f, true},
                                   {2.13f, 0.6f, true},
                                   {2.12f, 0.86f, true},
                                   {2.06f, 0.98f, true},
                                   {1.9f, 1.02f},
                                   {-0.78f, 1.02f},
                                   {-1.2f, 0.98f, true},
                                   {-1.7f, 0.93f, true},
                                   {-2.0f, 0.88f, true},
                                   {-2.1f, 0.8f, true},
                                   {-2.14f, 0.62f, true},
                                   {-2.12f, 0.42f, true}});
    return outline;
}

void AddWheel(Builder& builder, std::size_t tire, std::size_t rim) {
    constexpr float Radius = 0.33f;
    constexpr float Width = 0.22f;
    builder.Add(tire, Shapes::Wheel(Radius, Width, 48));
    // A dish turned around the axle, its face out at +X, with five spokes across it.
    const Matrix4 alongX = Matrix4::Rotation(Quaternion::FromAxisAngle({0, 0, 1}, -Spot::Pi * 0.5f));
    const LathePoint dish[] = {{0.0f, 0.085f},   {0.05f, 0.085f, true}, {0.07f, 0.075f, true}, {0.17f, 0.07f, true},
                               {0.195f, 0.085f}, {0.205f, 0.08f},       {0.205f, 0.04f},       {0.0f, 0.04f}};
    builder.Add(rim, Shapes::Lathe(dish, 48, 0.25f), alongX);
    for (int i = 0; i < 5; ++i) {
        const Quaternion around = Quaternion::FromAxisAngle({1, 0, 0}, 2.0f * Spot::Pi * static_cast<float>(i) / 5.0f);
        builder.Add(rim, Shapes::BeveledBox({0.025f, 0.04f, 0.13f}, 0.008f, 0.25f),
                    Matrix4::Translation({0.085f, 0.0f, 0.0f}) * Matrix4::Rotation(around) *
                        Matrix4::Translation({0.0f, 0.0f, 0.11f}));
    }
}

} // namespace

GltfModel Car() {
    Builder builder;
    // Glossy paint, and tinted glass that the interior shows through dimly.
    const std::size_t body =
        builder.AddMaterial({.name = "Body", .baseColor = {0.55f, 0.03f, 0.03f}, .roughness = 0.3f});
    const std::size_t glass = builder.AddMaterial(
        {.name = "Glass", .baseColor = {0.04f, 0.06f, 0.08f, 0.8f}, .roughness = 0.08f, .transparent = true});
    const std::size_t trim = builder.AddMaterial(SurfaceMaterial("Trim", Surface::Polymer, {0.09f, 0.09f, 0.1f, 1.0f}));
    const std::size_t interior =
        builder.AddMaterial(SurfaceMaterial("Interior", Surface::Polymer, {0.16f, 0.15f, 0.14f, 1.0f}));
    const std::size_t headlights = builder.AddMaterial(
        {.name = "Headlights", .baseColor = {1.0f, 0.97f, 0.9f}, .roughness = 0.2f, .emissive = {2.0f, 1.9f, 1.7f}});
    const std::size_t taillights = builder.AddMaterial(
        {.name = "Taillights", .baseColor = {0.6f, 0.03f, 0.02f}, .roughness = 0.3f, .emissive = {1.6f, 0.06f, 0.03f}});
    const std::size_t tire = builder.AddMaterial(SurfaceMaterial("Tire", Surface::Rubber, {0.5f, 0.5f, 0.5f, 1.0f}));
    const std::size_t rim =
        builder.AddMaterial(SurfaceMaterial("Rim", Surface::Gunmetal, {0.75f, 0.76f, 0.78f, 1.0f}, 0.5f));

    // The body, its windows above it, under a roof on pillars, with its mirrors.
    builder.Side(body, BodyOutline(), 1.78f);
    const ProfilePoint windows[] = {{-0.78f, 1.0f}, {1.9f, 1.0f},   {1.6f, 1.36f},
                                    {1.42f, 1.43f}, {0.02f, 1.45f}, {-0.14f, 1.42f}};
    builder.Side(glass, windows, 1.5f, 0.04f);
    const ProfilePoint roof[] = {{-0.08f, 1.4f}, {1.5f, 1.39f}, {1.47f, 1.45f}, {0.0f, 1.475f}, {-0.14f, 1.44f}};
    builder.Side(body, roof, 1.54f, 0.012f);
    // The rear quarters close the sides between the rear doors' windows and the hatch's.
    const ProfilePoint quarter[] = {{1.22f, 1.0f}, {1.9f, 1.0f}, {1.6f, 1.36f}, {1.42f, 1.43f}, {1.38f, 1.43f}};
    for (const float x : {-0.74f, 0.74f}) {
        builder.Strut(body, {x, 1.01f, -0.76f}, {x, 1.43f, -0.12f}, 0.07f, 0.07f);
        builder.Strut(body, {x, 1.0f, 0.55f}, {x, 1.43f, 0.55f}, 0.05f, 0.1f);
        builder.Add(body, Shapes::Extrude(quarter, 0.03f, TextureRepeat),
                    Matrix4::Translation({x * 1.01f, 0.0f, 0.0f}));
        builder.Block(body, {0.16f, 0.1f, 0.08f}, 0.02f, {x * 1.26f, 1.07f, -0.62f});
        builder.Block(trim, {0.08f, 0.03f, 0.04f}, 0.0f, {x * 1.13f, 1.05f, -0.6f});
    }

    // Black plastic: the grille, the bumpers' strips, the sills, the dark wells the wheels turn in, the moldings
    // under the windows, the doors' handles and the gaps around them.
    builder.Block(trim, {0.86f, 0.14f, 0.03f}, 0.0f, {0.0f, 0.58f, -2.135f});
    builder.Block(trim, {1.7f, 0.08f, 0.1f}, 0.02f, {0.0f, 0.35f, -2.06f});
    builder.Block(trim, {1.7f, 0.07f, 0.06f}, 0.02f, {0.0f, 0.48f, 2.12f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Block(trim, {0.03f, 0.08f, 1.7f}, 0.01f, {side * 0.895f, 0.34f, 0.0f});
        for (const float axle : {-1.3f, 1.3f}) {
            builder.Block(trim, {0.3f, 0.26f, 0.86f}, 0.0f, {side * 0.72f, 0.6f, axle});
        }
        builder.Block(trim, {0.02f, 0.025f, 2.66f}, 0.0f, {side * 0.755f, 1.02f, 0.56f});
        for (const float z : {-0.3f, 0.85f}) {
            builder.Block(trim, {0.025f, 0.03f, 0.14f}, 0.008f, {side * 0.895f, 0.9f, z});
        }
        for (const float z : {-0.75f, 0.5f, 1.62f}) {
            builder.Block(trim, {0.004f, 0.66f, 0.01f}, 0.0f, {side * 0.891f, 0.67f, z});
        }
    }

    // Seen through the glass: the floor, the dashboard, the seats and the steering wheel, on the left.
    builder.Block(interior, {1.46f, 0.01f, 2.56f}, 0.0f, {0.0f, 1.026f, 0.56f});
    builder.Block(interior, {1.4f, 0.14f, 0.34f}, 0.03f, {0.0f, 1.09f, -0.6f});
    for (const float x : {-0.38f, 0.38f}) {
        builder.Block(interior, {0.48f, 0.3f, 0.1f}, 0.03f, {x, 1.16f, 0.45f}, 12.0f);
        builder.Block(interior, {0.26f, 0.12f, 0.08f}, 0.03f, {x, 1.36f, 0.5f}, 12.0f);
    }
    builder.Block(interior, {1.3f, 0.26f, 0.1f}, 0.03f, {0.0f, 1.15f, 1.3f}, 10.0f);
    builder.Add(interior, Shapes::Cylinder(0.17f, 0.025f, 32),
                Matrix4::Translation({-0.38f, 1.17f, -0.38f}) *
                    Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, Spot::ToRadians(65.0f))));

    for (const float side : {-1.0f, 1.0f}) {
        builder.Block(headlights, {0.36f, 0.09f, 0.05f}, 0.015f, {side * 0.6f, 0.8f, -2.1f});
        builder.Block(taillights, {0.32f, 0.12f, 0.04f}, 0.012f, {side * 0.64f, 0.86f, 2.115f});
    }
    AddWheel(builder, tire, rim);
    return builder.Build();
}

GltfModel Coupe() {
    Builder builder;
    const std::size_t body = builder.AddMaterial(
        {.name = "Body", .baseColor = {0.025f, 0.16f, 0.48f}, .metallic = 0.65f, .roughness = 0.24f});
    const std::size_t glass = builder.AddMaterial(
        {.name = "Glass", .baseColor = {0.04f, 0.07f, 0.1f, 0.75f}, .roughness = 0.08f, .transparent = true});
    const std::size_t trim = builder.AddMaterial(SurfaceMaterial("Trim", Surface::Polymer, {0.06f, 0.07f, 0.08f}));
    const std::size_t interior =
        builder.AddMaterial(SurfaceMaterial("Interior", Surface::Polymer, {0.12f, 0.13f, 0.15f}));
    const std::size_t headlights = builder.AddMaterial(
        {.name = "Headlights", .baseColor = {0.9f, 0.96f, 1.0f}, .roughness = 0.15f, .emissive = {1.8f, 1.92f, 2.0f}});
    const std::size_t taillights = builder.AddMaterial({.name = "Taillights",
                                                        .baseColor = {0.65f, 0.02f, 0.025f},
                                                        .roughness = 0.2f,
                                                        .emissive = {1.8f, 0.04f, 0.05f}});
    const std::size_t tire = builder.AddMaterial(SurfaceMaterial("Tire", Surface::Rubber, {0.5f, 0.5f, 0.5f}));
    const std::size_t rim = builder.AddMaterial(SurfaceMaterial("Rim", Surface::Gunmetal, {0.85f, 0.87f, 0.9f}, 0.35f));

    constexpr float Axles[] = {-1.3f, 1.3f};
    auto outline = WheelArches(-2.14f, 0.3f, 0.42f, Axles);
    outline.insert(outline.end(), {{2.18f, 0.3f},
                                   {2.24f, 0.42f, true},
                                   {2.22f, 0.77f, true},
                                   {2.1f, 0.9f},
                                   {1.2f, 0.94f},
                                   {-0.72f, 0.9f},
                                   {-1.85f, 0.78f},
                                   {-2.2f, 0.65f, true},
                                   {-2.22f, 0.4f, true}});
    builder.Side(body, outline, 1.86f);
    const ProfilePoint windows[] = {{-0.72f, 0.9f}, {1.62f, 0.94f},  {0.82f, 1.26f},
                                    {0.62f, 1.31f}, {-0.06f, 1.31f}, {-0.22f, 1.25f}};
    builder.Side(glass, windows, 1.48f, 0.025f);
    const ProfilePoint roof[] = {{-0.19f, 1.27f}, {0.79f, 1.27f}, {0.68f, 1.35f}, {-0.06f, 1.35f}};
    builder.Side(body, roof, 1.54f, 0.015f);
    for (const float side : {-1.0f, 1.0f}) {
        const float x = side * 0.74f;
        builder.Strut(body, {x, 0.92f, -0.7f}, {x, 1.31f, -0.15f}, 0.06f, 0.06f);
        builder.Strut(trim, {x, 0.93f, 0.55f}, {x, 1.3f, 0.55f}, 0.035f, 0.06f);
        builder.Strut(body, {x, 1.3f, 0.71f}, {x, 0.95f, 1.61f}, 0.09f, 0.07f);
        builder.Block(body, {0.18f, 0.09f, 0.12f}, 0.025f, {side * 0.98f, 1.0f, -0.58f});
        builder.Block(trim, {0.035f, 0.08f, 1.7f}, 0.01f, {side * 0.935f, 0.34f, 0.0f});
        builder.Block(trim, {0.025f, 0.025f, 0.16f}, 0.006f, {side * 0.935f, 0.84f, 0.55f});
        for (const float z : {-0.72f, 0.85f}) {
            builder.Block(trim, {0.006f, 0.53f, 0.012f}, 0.0f, {side * 0.931f, 0.61f, z});
        }
        for (const float axle : Axles) {
            builder.Block(trim, {0.28f, 0.24f, 0.86f}, 0.0f, {side * 0.74f, 0.6f, axle});
        }
        builder.Block(headlights, {0.4f, 0.055f, 0.045f}, 0.012f, {side * 0.62f, 0.66f, -2.205f});
        builder.Block(taillights, {0.5f, 0.07f, 0.045f}, 0.012f, {side * 0.59f, 0.75f, 2.225f});
        builder.Block(trim, {0.22f, 0.06f, 0.1f}, 0.015f, {side * 0.62f, 0.36f, 2.24f});
    }
    builder.Block(trim, {1.12f, 0.16f, 0.04f}, 0.015f, {0.0f, 0.48f, -2.225f});
    builder.Block(trim, {1.82f, 0.045f, 0.16f}, 0.01f, {0.0f, 0.31f, -2.17f});
    builder.Block(trim, {1.6f, 0.12f, 0.06f}, 0.015f, {0.0f, 0.46f, 2.23f});
    builder.Block(body, {1.65f, 0.045f, 0.16f}, 0.012f, {0.0f, 0.94f, 2.07f});
    builder.Block(interior, {1.44f, 0.01f, 2.0f}, 0.0f, {0.0f, 0.95f, 0.4f});
    builder.Block(interior, {1.4f, 0.12f, 0.3f}, 0.025f, {0.0f, 1.0f, -0.52f});
    for (const float x : {-0.38f, 0.38f}) {
        builder.Block(interior, {0.48f, 0.23f, 0.1f}, 0.03f, {x, 1.06f, 0.4f}, 16.0f);
        builder.Block(interior, {0.24f, 0.1f, 0.08f}, 0.025f, {x, 1.22f, 0.45f}, 16.0f);
    }
    builder.Add(interior, Shapes::Cylinder(0.15f, 0.025f, 32),
                Matrix4::Translation({-0.38f, 1.07f, -0.3f}) *
                    Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, Spot::ToRadians(65.0f))));
    AddWheel(builder, tire, rim);
    return builder.Build();
}

GltfModel Helicopter() {
    Builder builder;
    const auto body = builder.AddMaterial(
        {.name = "Body", .baseColor = {0.92f, 0.23f, 0.035f}, .metallic = 0.4f, .roughness = 0.28f});
    const auto glass = builder.AddMaterial({.name = "Glass",
                                            .baseColor = {0.035f, 0.12f, 0.17f, 0.82f},
                                            .roughness = 0.08f,
                                            .doubleSided = true,
                                            .transparent = true});
    const auto metal = builder.AddMaterial(SurfaceMaterial("Metal", Surface::Gunmetal, {0.24f, 0.27f, 0.3f}, 0.4f));
    const auto interior = builder.AddMaterial(SurfaceMaterial("Interior", Surface::Polymer, {0.1f, 0.12f, 0.14f}));
    const auto accent = builder.AddMaterial({.name = "Accent", .baseColor = {0.95f, 0.92f, 0.8f}, .roughness = 0.4f});
    const auto red =
        builder.AddMaterial({.name = "NavRed", .baseColor = {0.8f, 0.02f, 0.015f}, .emissive = {2.0f, 0.02f, 0.01f}});
    const auto green =
        builder.AddMaterial({.name = "NavGreen", .baseColor = {0.02f, 0.8f, 0.15f}, .emissive = {0.02f, 2.0f, 0.15f}});
    const auto main = builder.AddMaterial(
        {.name = "MainRotor", .baseColor = {0.055f, 0.065f, 0.075f}, .metallic = 0.65f, .roughness = 0.35f});
    const auto mainTips =
        builder.AddMaterial({.name = "MainTips", .baseColor = {1.0f, 0.72f, 0.04f}, .roughness = 0.4f});
    const auto tail = builder.AddMaterial(
        {.name = "TailRotor", .baseColor = {0.055f, 0.065f, 0.075f}, .metallic = 0.65f, .roughness = 0.35f});
    const auto tailTips =
        builder.AddMaterial({.name = "TailTips", .baseColor = {1.0f, 0.72f, 0.04f}, .roughness = 0.4f});

    // Elliptical sections form a continuous rounded cabin; its forward upper panels are the cockpit glazing.
    struct Section {
        float z;
        float y;
        float width;
        float height;
    };
    constexpr Section Sections[] = {{-2.45f, 1.35f, 0.04f, 0.06f}, {-2.2f, 1.45f, 0.55f, 0.5f},
                                    {-1.65f, 1.6f, 0.88f, 0.85f},  {-0.65f, 1.62f, 0.98f, 0.98f},
                                    {0.55f, 1.6f, 0.92f, 0.9f},    {1.35f, 1.6f, 0.66f, 0.65f},
                                    {1.85f, 1.65f, 0.2f, 0.26f}};
    constexpr std::uint32_t Segments = 32;
    const auto vertex = [&](std::size_t ring, float angle) {
        const Section& section = Sections[ring];
        const Section& previous = Sections[ring == 0 ? 0 : ring - 1];
        const Section& next = Sections[std::min(ring + 1, std::size(Sections) - 1)];
        const float x = std::sin(angle);
        const float y = std::cos(angle);
        const float dz = next.z - previous.z;
        const Vector3 along = {(next.width - previous.width) * x / dz,
                               (next.y - previous.y + (next.height - previous.height) * y) / dz, 1.0f};
        const Vector3 around = {section.width * y, -section.height * x, 0.0f};
        return Spot::MeshVertex{.position = {section.width * x, section.y + section.height * y, section.z},
                                .normal = Spot::Normalize(Spot::Cross(along, around)),
                                .uv = {angle / (2.0f * Spot::Pi), (section.z + 2.45f) / 4.3f}};
    };
    for (std::size_t ring = 0; ring + 1 < std::size(Sections); ++ring) {
        for (std::uint32_t segment = 0; segment < Segments; ++segment) {
            const float a = static_cast<float>(segment) * 2.0f * Spot::Pi / Segments;
            const float b = static_cast<float>(segment + 1) * 2.0f * Spot::Pi / Segments;
            Spot::MeshData panel;
            panel.vertices = {vertex(ring, a), vertex(ring, b), vertex(ring + 1, b), vertex(ring + 1, a)};
            panel.indices = {0, 2, 1, 0, 3, 2};
            const bool window = ring < 4 && ring > 0 && std::cos((a + b) * 0.5f) > -0.05f;
            builder.Add(window ? glass : body, panel);
        }
    }
    for (const std::size_t ring : {std::size_t{0}, std::size(Sections) - 1}) {
        Spot::MeshData cap;
        const float sign = ring == 0 ? -1.0f : 1.0f;
        cap.vertices.push_back(
            {.position = {0.0f, Sections[ring].y, Sections[ring].z}, .normal = {0.0f, 0.0f, sign}, .uv = {0.5f, 0.5f}});
        for (std::uint32_t segment = 0; segment <= Segments; ++segment) {
            auto edge = vertex(ring, static_cast<float>(segment) * 2.0f * Spot::Pi / Segments);
            edge.normal = {0.0f, 0.0f, sign};
            cap.vertices.push_back(edge);
            if (segment > 0) {
                if (ring == 0) {
                    cap.indices.insert(cap.indices.end(), {0, segment, segment + 1});
                } else {
                    cap.indices.insert(cap.indices.end(), {0, segment + 1, segment});
                }
            }
        }
        builder.Add(body, cap);
    }
    // Frames follow the canopy's seams, leaving its seats and instrument panel visible through the glass.
    for (std::size_t ring = 1; ring <= 4; ++ring) {
        for (std::uint32_t segment = 0; segment < Segments; ++segment) {
            const float a = static_cast<float>(segment) * 2.0f * Spot::Pi / Segments;
            const float b = static_cast<float>(segment + 1) * 2.0f * Spot::Pi / Segments;
            if (std::cos((a + b) * 0.5f) > -0.05f) {
                builder.Strut(metal, vertex(ring, a).position, vertex(ring, b).position, 0.035f, 0.035f);
            }
        }
    }
    for (std::size_t ring = 1; ring < 4; ++ring) {
        builder.Strut(metal, vertex(ring, 0.0f).position, vertex(ring + 1, 0.0f).position, 0.045f, 0.045f);
    }
    builder.Block(body, {1.45f, 0.38f, 1.55f}, 0.12f, {0.0f, 2.47f, 0.55f});
    builder.Add(metal, Shapes::Cylinder(0.09f, 0.58f, 24), Matrix4::Translation({0.0f, 2.95f, 0.0f}));
    for (const float side : {-1.0f, 1.0f}) {
        for (int vent = 0; vent < 6; ++vent) {
            builder.Block(metal, {0.025f, 0.14f, 0.035f}, 0.0f,
                          {side * 0.73f, 2.46f, 0.2f + static_cast<float>(vent) * 0.13f});
        }
        builder.Block(metal, {0.06f, 0.055f, 0.22f}, 0.015f, {side * 0.965f, 1.45f, -0.25f});
        builder.Block(accent, {0.045f, 0.1f, 2.2f}, 0.01f, {side * 0.88f, 1.03f, -0.1f});
        builder.Block(interior, {0.56f, 0.14f, 0.6f}, 0.045f, {side * 0.43f, 1.05f, -0.5f});
        builder.Block(interior, {0.56f, 0.67f, 0.16f}, 0.055f, {side * 0.43f, 1.42f, -0.16f}, 10.0f);
        builder.Strut(metal, {side * 0.43f, 1.0f, -0.78f}, {side * 0.43f, 1.46f, -0.9f}, 0.035f, 0.035f);
        // Two long skids with upturned toes, supported by an angled pair of cross tubes.
        builder.Strut(metal, {side * 1.14f, 0.1f, -1.8f}, {side * 1.14f, 0.1f, 1.9f}, 0.13f, 0.16f);
        builder.Strut(metal, {side * 1.14f, 0.1f, -1.8f}, {side * 1.14f, 0.32f, -2.12f}, 0.13f, 0.16f);
        for (const float z : {-0.95f, 1.05f}) {
            builder.Strut(metal, {side * 0.6f, 0.95f, z}, {side * 1.14f, 0.18f, z}, 0.09f, 0.09f);
        }
    }
    builder.Block(interior, {1.25f, 0.32f, 0.35f}, 0.08f, {0.0f, 1.32f, -1.4f});
    builder.Block(metal, {0.72f, 0.2f, 0.025f}, 0.015f, {0.0f, 1.48f, -1.2f}, -15.0f);
    for (const float x : {-0.23f, 0.0f, 0.23f}) {
        builder.Block(green, {0.12f, 0.075f, 0.015f}, 0.01f, {x, 1.49f, -1.18f});
    }
    const Matrix4 alongZ = Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, Spot::Pi * 0.5f));
    builder.Add(body, Shapes::Frustum(0.32f, 0.085f, 4.35f, 32), Matrix4::Translation({0.0f, 1.83f, 3.9f}) * alongZ);
    const ProfilePoint fin[] = {{5.25f, 1.78f}, {6.25f, 1.78f}, {6.18f, 3.16f}, {5.85f, 3.16f}};
    builder.Side(body, fin, 0.11f, 0.025f);
    builder.Block(accent, {0.13f, 0.18f, 0.36f}, 0.02f, {0.0f, 2.94f, 5.98f});
    builder.Block(body, {2.45f, 0.085f, 0.44f}, 0.025f, {0.0f, 1.86f, 4.8f});
    builder.Block(accent, {0.15f, 0.095f, 0.44f}, 0.02f, {-1.15f, 1.86f, 4.8f});
    builder.Block(accent, {0.15f, 0.095f, 0.44f}, 0.02f, {1.15f, 1.86f, 4.8f});
    builder.Strut(metal, {0.0f, 2.1f, 5.9f}, {0.28f, 2.1f, 5.9f}, 0.12f, 0.12f);
    builder.Block(red, {0.1f, 0.09f, 0.13f}, 0.025f, {-0.98f, 1.68f, 0.25f});
    builder.Block(green, {0.1f, 0.09f, 0.13f}, 0.025f, {0.98f, 1.68f, 0.25f});
    builder.Block(red, {0.1f, 0.09f, 0.13f}, 0.025f, {0.0f, 2.71f, 0.75f});

    // Rotor meshes stay around zero; the prefab supplies their mast and tail pivots.
    builder.Add(main, Shapes::Cylinder(0.22f, 0.13f, 24));
    builder.Add(tail, Shapes::Cylinder(0.11f, 0.12f, 24),
                Matrix4::Rotation(Quaternion::FromAxisAngle({0, 0, 1}, Spot::Pi * 0.5f)));
    for (int blade = 0; blade < 4; ++blade) {
        const float angle = static_cast<float>(blade) * Spot::Pi * 0.5f;
        const Matrix4 aroundMain = Matrix4::Rotation(Quaternion::FromAxisAngle({0, 1, 0}, angle));
        builder.Add(main, Shapes::BeveledBox({4.0f, 0.045f, 0.25f}, 0.015f, TextureRepeat),
                    aroundMain * Matrix4::Translation({2.25f, 0.0f, 0.0f}));
        builder.Add(mainTips, Shapes::BeveledBox({0.35f, 0.046f, 0.25f}, 0.015f, TextureRepeat),
                    aroundMain * Matrix4::Translation({4.425f, 0.0f, 0.0f}));
        const Matrix4 aroundTail = Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, angle));
        builder.Add(tail, Shapes::Box({0.035f, 0.64f, 0.12f}, TextureRepeat),
                    aroundTail * Matrix4::Translation({0.0f, 0.43f, 0.0f}));
        builder.Add(tailTips, Shapes::Box({0.036f, 0.14f, 0.12f}, TextureRepeat),
                    aroundTail * Matrix4::Translation({0.0f, 0.82f, 0.0f}));
    }
    return builder.Build();
}

} // namespace Generator::Vehicles
