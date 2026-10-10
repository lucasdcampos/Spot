#include "weapons.h"

#include "shapes.h"
#include "textures.h"

#include <spot.h>

#include <string>
#include <utility>

namespace Generator::Weapons {

namespace {

using Spot::Matrix4;
using Spot::Vector3;
using Textures::Surface;

// A material of one of the surfaces' texture sets, tinted; the textures repeat every 0.25 meters on boxes.
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

// A model built part by part: each part gathers the shapes of one material.
class Builder {
public:
    std::size_t AddMaterial(GltfMaterial material) {
        m_model.materials.push_back(std::move(material));
        m_model.parts.push_back({.name = m_model.materials.back().name, .mesh = {}, .material = m_model.parts.size()});
        return m_model.parts.size() - 1;
    }

    void Box(std::size_t part, Vector3 size, Vector3 position, float tiltDegrees = 0.0f) {
        Shapes::Append(m_model.parts[part].mesh, Shapes::Box(size, 0.25f), Place(position, tiltDegrees));
    }

    // A cylinder along Z, as barrels and tubes lie.
    void Tube(std::size_t part, float radius, float length, Vector3 position) {
        Shapes::Append(m_model.parts[part].mesh, Shapes::Cylinder(radius, length, 12),
                       Matrix4::Translation(position) *
                           Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, Spot::Pi * 0.5f)));
    }

    // Without tangents, which the importer makes.
    GltfModel Build() { return std::move(m_model); }

private:
    // Tilted back about X, as grips lean.
    static Matrix4 Place(Vector3 position, float tiltDegrees) {
        return Matrix4::Translation(position) *
               Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, Spot::ToRadians(tiltDegrees)));
    }

    GltfModel m_model;
};

constexpr Spot::Color Gunmetal{0.22f, 0.23f, 0.25f};

GltfModel Pistol() {
    Builder builder;
    const std::size_t metal = builder.AddMaterial(SurfaceMaterial("Steel", Surface::PaintedMetal, Gunmetal));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Grip", Surface::Rubber));
    builder.Box(metal, {0.035f, 0.035f, 0.19f}, {0.0f, 0.03f, -0.06f});
    builder.Box(metal, {0.03f, 0.025f, 0.16f}, {0.0f, 0.0f, -0.05f});
    builder.Tube(metal, 0.008f, 0.03f, {0.0f, 0.03f, -0.165f});
    builder.Box(grip, {0.03f, 0.1f, 0.045f}, {0.0f, -0.055f, 0.0f}, -15.0f);
    return builder.Build();
}

GltfModel Rifle() {
    Builder builder;
    const std::size_t metal = builder.AddMaterial(SurfaceMaterial("Steel", Surface::PaintedMetal, Gunmetal));
    const std::size_t wood = builder.AddMaterial(SurfaceMaterial("Stock", Surface::Wood));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Grip", Surface::Rubber));
    builder.Box(metal, {0.05f, 0.07f, 0.35f}, {0.0f, 0.0f, -0.1f});
    builder.Tube(metal, 0.012f, 0.4f, {0.0f, 0.01f, -0.47f});
    builder.Tube(metal, 0.018f, 0.16f, {0.0f, 0.06f, -0.1f});
    builder.Box(metal, {0.03f, 0.12f, 0.06f}, {0.0f, -0.09f, -0.12f}, 10.0f);
    builder.Box(wood, {0.04f, 0.08f, 0.25f}, {0.0f, -0.02f, 0.2f});
    builder.Box(grip, {0.03f, 0.09f, 0.04f}, {0.0f, -0.07f, 0.03f}, -15.0f);
    return builder.Build();
}

GltfModel Shotgun() {
    Builder builder;
    const std::size_t metal = builder.AddMaterial(SurfaceMaterial("Steel", Surface::RustyMetal));
    const std::size_t wood = builder.AddMaterial(SurfaceMaterial("Wood", Surface::Wood));
    builder.Box(metal, {0.05f, 0.06f, 0.25f}, {0.0f, 0.0f, -0.05f});
    builder.Tube(metal, 0.014f, 0.5f, {0.0f, 0.015f, -0.42f});
    builder.Tube(metal, 0.011f, 0.4f, {0.0f, -0.015f, -0.37f});
    builder.Box(wood, {0.05f, 0.045f, 0.14f}, {0.0f, -0.015f, -0.35f});
    builder.Box(wood, {0.045f, 0.09f, 0.28f}, {0.0f, -0.03f, 0.2f});
    return builder.Build();
}

GltfModel RocketLauncher() {
    Builder builder;
    const std::size_t tube =
        builder.AddMaterial(SurfaceMaterial("Tube", Surface::PaintedMetal, {0.3f, 0.36f, 0.2f, 1.0f}));
    const std::size_t metal = builder.AddMaterial(SurfaceMaterial("Steel", Surface::PaintedMetal, Gunmetal));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Grip", Surface::Rubber));
    builder.Tube(tube, 0.06f, 0.9f, {0.0f, 0.04f, -0.15f});
    builder.Box(metal, {0.02f, 0.05f, 0.08f}, {0.07f, 0.1f, -0.2f});
    builder.Box(grip, {0.03f, 0.1f, 0.045f}, {0.0f, -0.06f, -0.05f}, -15.0f);
    return builder.Build();
}

GltfModel ToolGun() {
    Builder builder;
    const std::size_t body =
        builder.AddMaterial(SurfaceMaterial("Body", Surface::PaintedMetal, {0.85f, 0.85f, 0.88f, 1.0f}));
    const std::size_t metal = builder.AddMaterial(SurfaceMaterial("Emitter", Surface::RustyMetal));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Grip", Surface::Rubber));
    GltfMaterial screen = SurfaceMaterial("Screen", Surface::Glass, {0.05f, 0.08f, 0.1f, 1.0f});
    screen.emissive = {0.2f, 1.6f, 2.0f, 1.0f};
    const std::size_t glow = builder.AddMaterial(std::move(screen));
    builder.Box(body, {0.06f, 0.08f, 0.22f}, {0.0f, 0.01f, -0.08f});
    builder.Tube(metal, 0.02f, 0.08f, {0.0f, 0.02f, -0.23f});
    builder.Box(glow, {0.045f, 0.05f, 0.004f}, {0.0f, 0.02f, 0.031f});
    builder.Box(grip, {0.035f, 0.1f, 0.045f}, {0.0f, -0.07f, 0.0f}, -15.0f);
    return builder.Build();
}

GltfModel PhysicsGun() {
    Builder builder;
    const std::size_t body =
        builder.AddMaterial(SurfaceMaterial("Body", Surface::PaintedMetal, {0.95f, 0.55f, 0.1f, 1.0f}));
    const std::size_t metal = builder.AddMaterial(SurfaceMaterial("Prongs", Surface::PaintedMetal, Gunmetal));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Grip", Surface::Rubber));
    GltfMaterial core = SurfaceMaterial("Core", Surface::Glass, {0.1f, 0.15f, 0.2f, 1.0f});
    core.emissive = {0.8f, 2.0f, 3.0f, 1.0f};
    const std::size_t glow = builder.AddMaterial(std::move(core));
    builder.Tube(body, 0.04f, 0.3f, {0.0f, 0.02f, -0.1f});
    builder.Tube(glow, 0.02f, 0.1f, {0.0f, 0.02f, -0.3f});
    for (const float angle : {0.0f, 120.0f, 240.0f}) {
        const float radians = Spot::ToRadians(angle);
        builder.Box(metal, {0.012f, 0.012f, 0.12f},
                    {std::sin(radians) * 0.035f, 0.02f + std::cos(radians) * 0.035f, -0.31f});
    }
    builder.Box(grip, {0.035f, 0.1f, 0.045f}, {0.0f, -0.07f, 0.0f}, -15.0f);
    return builder.Build();
}

} // namespace

std::vector<WeaponModel> MakeWeaponModels() {
    std::vector<WeaponModel> models;
    models.push_back({"pistol", Pistol()});
    models.push_back({"rifle", Rifle()});
    models.push_back({"shotgun", Shotgun()});
    models.push_back({"rocket_launcher", RocketLauncher()});
    models.push_back({"tool_gun", ToolGun()});
    models.push_back({"physics_gun", PhysicsGun()});
    return models;
}

} // namespace Generator::Weapons
