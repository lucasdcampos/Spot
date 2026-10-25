#include "vehicles.h"

#include "shapes.h"
#include "textures.h"

#include <spot.h>

#include <cmath>
#include <initializer_list>
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

// The body's side: low at the nose, along the hood up to the windshield, flat under the windows to the tail, its
// bottom cut by an arch over each wheel. Counterclockwise from the nose's bottom, Z to the rear.
std::vector<ProfilePoint> BodyOutline() {
    constexpr float Bottom = 0.3f;
    constexpr float ArchRadius = 0.42f;
    std::vector<ProfilePoint> outline = {{-2.02f, Bottom}};
    for (const float axle : {-1.3f, 1.3f}) {
        // Over the wheel from its front to its rear, rounding the arch's inside.
        constexpr int Segments = 12;
        for (int i = 0; i <= Segments; ++i) {
            const float angle = Spot::Pi * (1.0f - static_cast<float>(i) / Segments);
            outline.push_back(
                {axle + ArchRadius * std::cos(angle), Bottom + ArchRadius * std::sin(angle), i > 0 && i < Segments});
        }
    }
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

} // namespace Generator::Vehicles
