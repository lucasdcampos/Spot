#include "weapons.h"

#include "shapes.h"
#include "textures.h"

#include <spot.h>

#include <string>
#include <utility>

namespace Generator::Weapons {

namespace {

using Spot::Matrix4;
using Spot::Quaternion;
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

// A plain material that glows, such as sights' dots and screens.
GltfMaterial GlowMaterial(std::string name, Spot::Color color, Spot::Color emissive) {
    return {.name = std::move(name), .baseColor = color, .roughness = 0.4f, .emissive = emissive};
}

constexpr float TextureRepeat = 0.25f;

// A model built part by part: each part gathers the shapes of one material. Parts are named after their material, so
// the game finds the moving ones, such as a pistol's "Slide", by name.
class Builder {
public:
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
        Add(part, Shapes::BeveledBox(size, bevel, TextureRepeat), Place(position, tiltDegrees));
    }

    // A cylinder along Z, as barrels and tubes lie.
    void Tube(std::size_t part, float radius, float length, Vector3 position, std::uint32_t segments = 20) {
        Add(part, Shapes::Cylinder(radius, length, segments), Matrix4::Translation(position) * AlongZ());
    }

    // A cone along Z with its tip toward -Z, as a warhead points, or toward +Z backward.
    void Nose(std::size_t part, float radius, float length, Vector3 position, bool backward = false) {
        const float angle = backward ? Spot::Pi * 0.5f : -Spot::Pi * 0.5f;
        Add(part, Shapes::Cone(radius, length, 24),
            Matrix4::Translation(position) * Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, angle)));
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
};

constexpr Spot::Color Gunmetal{0.22f, 0.23f, 0.25f};
constexpr Spot::Color Blackened{0.12f, 0.12f, 0.13f};
constexpr Spot::Color Steel{0.45f, 0.46f, 0.48f};

GltfModel Pistol() {
    Builder builder;
    const std::size_t slide = builder.AddMaterial(SurfaceMaterial("Slide", Surface::PaintedMetal, Steel));
    const std::size_t details = builder.AddMaterial(SurfaceMaterial("Slide Details", Surface::PaintedMetal, Blackened));
    const std::size_t dots = builder.AddMaterial(GlowMaterial("Slide Dots", {0.3f, 0.9f, 0.4f}, {0.5f, 2.5f, 0.8f}));
    const std::size_t frame = builder.AddMaterial(SurfaceMaterial("Frame", Surface::Rubber));
    const std::size_t barrel = builder.AddMaterial(SurfaceMaterial("Barrel", Surface::PaintedMetal, Blackened));
    const std::size_t magazine = builder.AddMaterial(SurfaceMaterial("Magazine", Surface::PaintedMetal, Gunmetal));

    // The slide, with its grip serrations, ejection port and three-dot sights, all of which cycle when it fires.
    builder.Block(slide, {0.03f, 0.032f, 0.2f}, 0.004f, {0.0f, 0.03f, -0.07f});
    for (int i = 0; i < 6; ++i) {
        builder.Box(details, {0.0312f, 0.024f, 0.0025f}, {0.0f, 0.031f, 0.0f + 0.005f * static_cast<float>(i)});
    }
    builder.Box(details, {0.002f, 0.012f, 0.04f}, {0.0151f, 0.036f, -0.03f});
    builder.Box(details, {0.004f, 0.007f, 0.006f}, {0.0f, 0.0495f, -0.155f});
    builder.Box(dots, {0.0025f, 0.0025f, 0.0015f}, {0.0f, 0.0505f, -0.1515f});
    builder.Box(details, {0.026f, 0.004f, 0.007f}, {0.0f, 0.048f, 0.02f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Box(details, {0.008f, 0.008f, 0.007f}, {side * 0.0075f, 0.05f, 0.02f});
        builder.Box(dots, {0.0025f, 0.0025f, 0.0015f}, {side * 0.0075f, 0.0515f, 0.0236f});
    }
    builder.Tube(barrel, 0.0065f, 0.012f, {0.0f, 0.032f, -0.171f}, 16);

    builder.Block(frame, {0.028f, 0.02f, 0.17f}, 0.004f, {0.0f, 0.004f, -0.06f});
    builder.Box(frame, {0.02f, 0.008f, 0.05f}, {0.0f, -0.008f, -0.12f});
    builder.Box(frame, {0.008f, 0.005f, 0.05f}, {0.0f, -0.03f, -0.055f});
    builder.Box(frame, {0.008f, 0.03f, 0.005f}, {0.0f, -0.016f, -0.08f});
    builder.Box(barrel, {0.005f, 0.018f, 0.006f}, {0.0f, -0.012f, -0.045f}, 10.0f);
    builder.Block(frame, {0.03f, 0.105f, 0.046f}, 0.006f, {0.0f, -0.055f, 0.008f}, -16.0f);

    // The magazine hides in the grip but for its base plate, until a reload drops it out.
    builder.Block(magazine, {0.022f, 0.1f, 0.034f}, 0.003f, {0.0f, -0.058f, 0.01f}, -16.0f);
    builder.Block(magazine, {0.032f, 0.007f, 0.05f}, 0.002f, {0.0f, -0.1088f, 0.0235f}, -16.0f);

    builder.Marker("Muzzle", {0.0f, 0.032f, -0.178f});
    builder.Marker("Eye", {0.0f, 0.0525f, 0.37f});
    return builder.Build();
}

GltfModel Rifle() {
    Builder builder;
    const std::size_t receiver =
        builder.AddMaterial(SurfaceMaterial("Receiver", Surface::PaintedMetal, {0.27f, 0.28f, 0.26f, 1.0f}));
    const std::size_t furniture = builder.AddMaterial(SurfaceMaterial("Furniture", Surface::Rubber));
    const std::size_t barrel = builder.AddMaterial(SurfaceMaterial("Barrel", Surface::PaintedMetal, Blackened));
    const std::size_t optic =
        builder.AddMaterial(SurfaceMaterial("Optic", Surface::PaintedMetal, {0.16f, 0.16f, 0.17f}));
    GltfMaterial lens = SurfaceMaterial("Optic Glass", Surface::Glass, {0.55f, 0.75f, 0.85f, 0.22f});
    lens.metallic = 0.0f;
    lens.transparent = true;
    const std::size_t glass = builder.AddMaterial(std::move(lens));
    const std::size_t reticle = builder.AddMaterial(GlowMaterial("Reticle", {1.0f, 0.1f, 0.05f}, {12.0f, 0.4f, 0.2f}));
    const std::size_t magazine =
        builder.AddMaterial(SurfaceMaterial("Magazine", Surface::PaintedMetal, {0.42f, 0.37f, 0.27f, 1.0f}));

    builder.Block(receiver, {0.044f, 0.05f, 0.3f}, 0.005f, {0.0f, 0.02f, -0.07f});
    builder.Block(receiver, {0.04f, 0.045f, 0.2f}, 0.005f, {0.0f, -0.02f, -0.04f});
    builder.Box(receiver, {0.034f, 0.03f, 0.08f}, {0.0f, -0.045f, -0.11f});
    builder.Box(receiver, {0.022f, 0.008f, 0.42f}, {0.0f, 0.049f, -0.13f});
    for (int i = 0; i < 14; ++i) {
        builder.Box(barrel, {0.024f, 0.004f, 0.007f}, {0.0f, 0.054f, 0.06f - 0.03f * static_cast<float>(i)});
    }
    builder.Box(receiver, {0.03f, 0.008f, 0.02f}, {0.0f, 0.04f, 0.09f});
    builder.Box(receiver, {0.01f, 0.004f, 0.06f}, {0.0f, -0.06f, -0.02f});
    builder.Box(barrel, {0.005f, 0.02f, 0.006f}, {0.0f, -0.05f, -0.02f}, 10.0f);

    builder.Block(furniture, {0.052f, 0.052f, 0.27f}, 0.008f, {0.0f, 0.018f, -0.355f});
    for (const float side : {-1.0f, 1.0f}) {
        for (int i = 0; i < 4; ++i) {
            builder.Box(barrel, {0.002f, 0.012f, 0.03f},
                        {side * 0.0262f, 0.02f, -0.27f - 0.05f * static_cast<float>(i)});
        }
    }
    builder.Tube(barrel, 0.009f, 0.16f, {0.0f, 0.02f, -0.57f});
    builder.Tube(barrel, 0.014f, 0.025f, {0.0f, 0.02f, -0.505f});
    builder.Tube(barrel, 0.0135f, 0.05f, {0.0f, 0.02f, -0.675f});

    builder.Block(furniture, {0.03f, 0.1f, 0.042f}, 0.007f, {0.0f, -0.075f, 0.035f}, -20.0f);
    builder.Tube(receiver, 0.015f, 0.16f, {0.0f, 0.015f, 0.16f});
    builder.Block(furniture, {0.04f, 0.075f, 0.16f}, 0.01f, {0.0f, -0.005f, 0.25f});
    builder.Box(furniture, {0.042f, 0.09f, 0.015f}, {0.0f, -0.01f, 0.335f});

    builder.Block(magazine, {0.026f, 0.16f, 0.07f}, 0.004f, {0.0f, -0.1f, -0.11f}, 14.0f);

    // A reflex sight: an open hood around a pane whose dot aiming puts on the crosshair.
    builder.Box(optic, {0.032f, 0.012f, 0.06f}, {0.0f, 0.059f, -0.06f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Block(optic, {0.006f, 0.042f, 0.05f}, 0.002f, {side * 0.0175f, 0.084f, -0.06f});
    }
    builder.Block(optic, {0.041f, 0.007f, 0.05f}, 0.002f, {0.0f, 0.1075f, -0.06f});
    builder.Box(glass, {0.029f, 0.036f, 0.002f}, {0.0f, 0.085f, -0.075f});
    builder.Box(reticle, {0.0016f, 0.0016f, 0.0005f}, {0.0f, 0.085f, -0.0738f});

    builder.Marker("Muzzle", {0.0f, 0.02f, -0.705f});
    builder.Marker("Eye", {0.0f, 0.085f, 0.17f});
    return builder.Build();
}

GltfModel Shotgun() {
    Builder builder;
    const std::size_t receiver = builder.AddMaterial(SurfaceMaterial("Receiver", Surface::PaintedMetal, Gunmetal));
    const std::size_t barrel = builder.AddMaterial(SurfaceMaterial("Barrel", Surface::PaintedMetal, Blackened));
    const std::size_t stock = builder.AddMaterial(SurfaceMaterial("Stock", Surface::Wood));
    const std::size_t pump = builder.AddMaterial(SurfaceMaterial("Pump", Surface::Wood));
    const std::size_t grooves = builder.AddMaterial(SurfaceMaterial("Pump Grooves", Surface::Wood, {0.3f, 0.3f, 0.3f}));
    const std::size_t rubber = builder.AddMaterial(SurfaceMaterial("Butt Pad", Surface::Rubber));
    GltfMaterial brass = SurfaceMaterial("Bead", Surface::PaintedMetal, {0.95f, 0.78f, 0.4f, 1.0f});
    brass.emissive = {0.25f, 0.18f, 0.06f, 1.0f};
    const std::size_t bead = builder.AddMaterial(std::move(brass));

    builder.Block(receiver, {0.046f, 0.06f, 0.22f}, 0.006f, {0.0f, 0.0f, -0.03f});
    builder.Box(barrel, {0.002f, 0.016f, 0.06f}, {0.0231f, 0.008f, -0.04f});
    builder.Tube(barrel, 0.0125f, 0.52f, {0.0f, 0.016f, -0.4f});
    builder.Tube(barrel, 0.011f, 0.42f, {0.0f, -0.014f, -0.35f});
    builder.Box(receiver, {0.03f, 0.05f, 0.015f}, {0.0f, 0.001f, -0.56f});
    builder.Box(bead, {0.004f, 0.005f, 0.004f}, {0.0f, 0.031f, -0.645f});

    // The forend the player pumps after each shot.
    builder.Block(pump, {0.05f, 0.042f, 0.17f}, 0.01f, {0.0f, -0.014f, -0.33f});
    for (int i = 0; i < 5; ++i) {
        builder.Box(grooves, {0.051f, 0.004f, 0.005f}, {0.0f, -0.014f, -0.27f - 0.03f * static_cast<float>(i)});
    }

    builder.Block(stock, {0.04f, 0.07f, 0.075f}, 0.012f, {0.0f, -0.035f, 0.09f}, -25.0f);
    builder.Block(stock, {0.044f, 0.08f, 0.3f}, 0.012f, {0.0f, -0.04f, 0.24f}, 8.0f);
    builder.Box(rubber, {0.046f, 0.085f, 0.02f}, {0.0f, -0.0623f, 0.3984f}, 8.0f);
    builder.Box(receiver, {0.01f, 0.004f, 0.06f}, {0.0f, -0.045f, 0.02f});
    builder.Box(barrel, {0.005f, 0.02f, 0.006f}, {0.0f, -0.035f, 0.02f}, 10.0f);

    builder.Marker("Muzzle", {0.0f, 0.016f, -0.665f});
    builder.Marker("Eye", {0.0f, 0.0335f, 0.27f});
    return builder.Build();
}

GltfModel RocketLauncher() {
    Builder builder;
    const std::size_t tube =
        builder.AddMaterial(SurfaceMaterial("Launcher Tube", Surface::PaintedMetal, {0.3f, 0.36f, 0.2f, 1.0f}));
    const std::size_t fittings =
        builder.AddMaterial(SurfaceMaterial("Launcher Fittings", Surface::PaintedMetal, Gunmetal));
    const std::size_t band =
        builder.AddMaterial(SurfaceMaterial("Launcher Band", Surface::PaintedMetal, {0.95f, 0.72f, 0.1f, 1.0f}));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Launcher Grip", Surface::Rubber));
    const std::size_t body = builder.AddMaterial(SurfaceMaterial("Rocket Body", Surface::PaintedMetal, Steel));
    const std::size_t warhead =
        builder.AddMaterial(SurfaceMaterial("Rocket Warhead", Surface::PaintedMetal, {0.24f, 0.28f, 0.16f, 1.0f}));

    builder.Tube(tube, 0.06f, 0.95f, {0.0f, 0.05f, -0.1f}, 28);
    builder.Tube(fittings, 0.068f, 0.05f, {0.0f, 0.05f, -0.56f}, 28);
    builder.Tube(fittings, 0.07f, 0.07f, {0.0f, 0.05f, 0.38f}, 28);
    builder.Tube(band, 0.0605f, 0.04f, {0.0f, 0.05f, -0.46f}, 28);
    builder.Tube(band, 0.0605f, 0.04f, {0.0f, 0.05f, 0.3f}, 28);
    builder.Box(fittings, {0.03f, 0.025f, 0.06f}, {0.0f, -0.005f, -0.04f});
    builder.Block(grip, {0.032f, 0.1f, 0.045f}, 0.007f, {0.0f, -0.045f, -0.02f}, -15.0f);
    builder.Block(grip, {0.03f, 0.09f, 0.04f}, 0.007f, {0.0f, -0.045f, -0.25f}, -5.0f);
    builder.Box(fittings, {0.01f, 0.004f, 0.05f}, {0.0f, -0.035f, -0.065f});

    // Iron sights on the left of the tube: a post in front and an aperture behind.
    builder.Box(fittings, {0.03f, 0.012f, 0.02f}, {-0.055f, 0.085f, -0.35f});
    builder.Box(fittings, {0.005f, 0.025f, 0.005f}, {-0.07f, 0.1f, -0.35f});
    builder.Box(fittings, {0.03f, 0.012f, 0.02f}, {-0.055f, 0.085f, 0.05f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Box(fittings, {0.026f, 0.005f, 0.005f}, {-0.07f, 0.1125f + side * 0.0105f, 0.05f});
        builder.Box(fittings, {0.005f, 0.026f, 0.005f}, {-0.07f + side * 0.0105f, 0.1125f, 0.05f});
    }

    // The loaded rocket, sticking out of the front until it is fired.
    builder.Tube(body, 0.035f, 0.12f, {0.0f, 0.05f, -0.6f});
    builder.Tube(warhead, 0.045f, 0.06f, {0.0f, 0.05f, -0.69f});
    builder.Nose(warhead, 0.045f, 0.12f, {0.0f, 0.05f, -0.78f});

    builder.Marker("Muzzle", {0.0f, 0.05f, -0.59f});
    builder.Marker("Eye", {-0.07f, 0.1125f, 0.34f});
    // Carried on the shoulder, further right and lower than what is held in the hand.
    builder.Marker("Hip", {-0.07f, 0.05f, 0.12f});
    return builder.Build();
}

GltfModel ToolGun() {
    Builder builder;
    const std::size_t body =
        builder.AddMaterial(SurfaceMaterial("Body", Surface::PaintedMetal, {0.85f, 0.85f, 0.88f, 1.0f}));
    const std::size_t trim =
        builder.AddMaterial(SurfaceMaterial("Trim", Surface::PaintedMetal, {0.15f, 0.17f, 0.2f, 1.0f}));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Grip", Surface::Rubber));
    const std::size_t glow = builder.AddMaterial(GlowMaterial("Glow", {0.1f, 0.6f, 0.8f}, {0.3f, 2.4f, 3.2f}));
    const std::size_t screen = builder.AddMaterial(GlowMaterial("Screen", {0.05f, 0.08f, 0.1f}, {0.15f, 1.0f, 1.3f}));

    builder.Block(body, {0.055f, 0.075f, 0.22f}, 0.012f, {0.0f, 0.015f, -0.07f});
    builder.Block(trim, {0.012f, 0.03f, 0.14f}, 0.004f, {0.0f, 0.06f, -0.08f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Box(glow, {0.002f, 0.008f, 0.15f}, {side * 0.0281f, 0.02f, -0.08f});
    }
    builder.Tube(trim, 0.026f, 0.012f, {0.0f, 0.015f, -0.186f});
    builder.Tube(trim, 0.02f, 0.06f, {0.0f, 0.015f, -0.21f});
    builder.Tube(glow, 0.012f, 0.03f, {0.0f, 0.015f, -0.25f});
    builder.Box(screen, {0.04f, 0.045f, 0.003f}, {0.0f, 0.02f, 0.0405f});
    builder.Block(grip, {0.034f, 0.1f, 0.045f}, 0.008f, {0.0f, -0.07f, 0.0f}, -15.0f);

    builder.Marker("Muzzle", {0.0f, 0.015f, -0.266f});
    builder.Marker("Eye", {0.0f, 0.08f, 0.3f});
    return builder.Build();
}

GltfModel PhysicsGun() {
    Builder builder;
    const std::size_t body =
        builder.AddMaterial(SurfaceMaterial("Body", Surface::PaintedMetal, {0.95f, 0.55f, 0.1f, 1.0f}));
    const std::size_t trim = builder.AddMaterial(SurfaceMaterial("Trim", Surface::PaintedMetal, Gunmetal));
    const std::size_t prongs = builder.AddMaterial(SurfaceMaterial("Prongs", Surface::PaintedMetal, Blackened));
    const std::size_t grip = builder.AddMaterial(SurfaceMaterial("Grip", Surface::Rubber));
    const std::size_t core = builder.AddMaterial(GlowMaterial("Core", {0.1f, 0.15f, 0.2f}, {0.8f, 2.0f, 3.0f}));

    builder.Tube(body, 0.042f, 0.26f, {0.0f, 0.02f, -0.08f}, 24);
    builder.Tube(trim, 0.045f, 0.02f, {0.0f, 0.02f, 0.055f}, 24);
    builder.Nose(trim, 0.045f, 0.012f, {0.0f, 0.02f, 0.071f}, true);
    builder.Tube(trim, 0.05f, 0.04f, {0.0f, 0.02f, -0.22f}, 24);
    for (const float side : {-1.0f, 1.0f}) {
        builder.Tube(trim, 0.006f, 0.2f, {side * 0.045f, 0.035f, -0.09f}, 10);
    }
    builder.Tube(core, 0.022f, 0.12f, {0.0f, 0.02f, -0.29f});
    for (const float angle : {0.0f, 120.0f, 240.0f}) {
        const float radians = Spot::ToRadians(angle);
        builder.Block(prongs, {0.012f, 0.012f, 0.16f}, 0.003f,
                      {std::sin(radians) * 0.04f, 0.02f + std::cos(radians) * 0.04f, -0.31f});
    }
    builder.Block(grip, {0.035f, 0.1f, 0.045f}, 0.008f, {0.0f, -0.07f, 0.0f}, -15.0f);

    builder.Marker("Muzzle", {0.0f, 0.02f, -0.39f});
    builder.Marker("Eye", {0.0f, 0.075f, 0.3f});
    builder.Marker("Hip", {0.0f, 0.0f, 0.08f});
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
