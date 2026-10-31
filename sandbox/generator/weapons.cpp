#include "weapons.h"

#include "shapes.h"
#include "textures.h"

#include <spot.h>

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

    // A side view, as Shapes::Extrude() takes profiles, Z toward the back and Y up, as wide as given across X, its
    // edges beveled: the outline of a slide, a grip or a stock.
    void Profile(std::size_t part, std::initializer_list<ProfilePoint> profile, float width, float bevel,
                 Vector3 position, float tiltDegrees = 0.0f) {
        Add(part, Shapes::BeveledExtrude(std::span(profile.begin(), profile.size()), width, bevel, TextureRepeat),
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
        Add(part, Shapes::BeveledExtrude(section, length, bevel, TextureRepeat),
            Matrix4::Translation(position) * Matrix4::Rotation(Quaternion::FromAxisAngle({0, 1, 0}, Spot::Pi * 0.5f)));
    }

    // A profile turned around Z, its top toward the back (+Z), as barrels, muzzles and rockets are turned; spun around
    // its axis by the angle, so the flats of few segments face where they should.
    void Turned(std::size_t part, std::initializer_list<LathePoint> profile, Vector3 position,
                std::uint32_t segments = 32, float spinDegrees = 0.0f) {
        Add(part, Shapes::Lathe(std::span(profile.begin(), profile.size()), segments, TextureRepeat),
            Matrix4::Translation(position) * AlongZ() *
                Matrix4::Rotation(Quaternion::FromAxisAngle({0, 1, 0}, Spot::ToRadians(spinDegrees))));
    }

    // A cylinder along Z, as barrels and tubes lie.
    void Tube(std::size_t part, float radius, float length, Vector3 position, std::uint32_t segments = 32) {
        Add(part, Shapes::Cylinder(radius, length, segments), Matrix4::Translation(position) * AlongZ());
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
        builder.Box(details, {0.0306f, 0.02f, 0.0016f}, {0.0f, 0.029f, 0.023f - 0.0034f * static_cast<float>(i)});
    }
    for (int i = 0; i < 5; ++i) {
        builder.Box(details, {0.0306f, 0.016f, 0.0016f}, {0.0f, 0.027f, -0.115f - 0.0034f * static_cast<float>(i)});
    }
    builder.Block(details, {0.0024f, 0.012f, 0.04f}, 0.0008f, {0.0143f, 0.036f, -0.03f});
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
                   {0.0f, 0.032f, -0.171f}, 24);

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
        builder.AddMaterial(SurfaceMaterial("Receiver", Surface::Gunmetal, {0.27f, 0.28f, 0.26f, 1.0f}));
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
    builder.Profile(
        receiver,
        {{-0.22f, -0.005f}, {0.08f, -0.005f}, {0.08f, 0.04f}, {0.075f, 0.045f}, {-0.215f, 0.045f}, {-0.22f, 0.04f}},
        0.044f, 0.004f, {});
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
    builder.Block(receiver, {0.022f, 0.008f, 0.42f}, 0.0015f, {0.0f, 0.049f, -0.13f});
    for (int i = 0; i < 14; ++i) {
        builder.Block(barrel, {0.024f, 0.004f, 0.007f}, 0.001f, {0.0f, 0.054f, 0.06f - 0.03f * static_cast<float>(i)});
    }
    builder.Block(receiver, {0.03f, 0.008f, 0.02f}, 0.002f, {0.0f, 0.04f, 0.09f});
    builder.Turned(receiver, {{0.0f, 0.014f}, {0.007f, 0.014f}, {0.007f, -0.014f}, {0.0f, -0.014f}},
                   {0.025f, 0.028f, 0.035f}, 16);
    builder.Box(barrel, {0.002f, 0.016f, 0.05f}, {0.0225f, 0.022f, -0.02f});
    builder.Block(receiver, {0.01f, 0.004f, 0.06f}, 0.0012f, {0.0f, -0.06f, -0.02f});
    builder.Block(barrel, {0.005f, 0.02f, 0.006f}, 0.0015f, {0.0f, -0.05f, -0.02f}, 10.0f);

    // An octagonal handguard with slots along its sides, and the barrel, gas block and flash hider ahead of it.
    builder.Turned(
        furniture,
        {{0.0f, 0.135f}, {0.026f, 0.135f}, {0.028f, 0.13f}, {0.028f, -0.13f}, {0.026f, -0.135f}, {0.0f, -0.135f}},
        {0.0f, 0.018f, -0.355f}, 8, 22.5f);
    for (const float side : {-1.0f, 1.0f}) {
        for (int i = 0; i < 4; ++i) {
            builder.Box(barrel, {0.002f, 0.009f, 0.03f},
                        {side * 0.0262f, 0.018f, -0.27f - 0.05f * static_cast<float>(i)});
        }
    }
    builder.Turned(barrel, {{0.0f, 0.08f}, {0.0095f, 0.08f}, {0.0085f, -0.08f}, {0.0f, -0.08f}}, {0.0f, 0.02f, -0.57f});
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
        furniture, {{0.17f, -0.035f}, {0.33f, -0.055f}, {0.335f, 0.03f}, {0.31f, 0.04f}, {0.2f, 0.04f}, {0.17f, 0.03f}},
        0.04f, 0.008f, {0.0f, -0.005f, 0.0f});
    builder.Block(furniture, {0.042f, 0.09f, 0.015f}, 0.004f, {0.0f, -0.01f, 0.335f});

    // A curved magazine, as rifles' are.
    builder.Profile(magazine,
                    {{-0.035f, 0.08f},
                     {-0.037f, 0.0f, true},
                     {-0.045f, -0.08f},
                     {0.025f, -0.08f},
                     {0.033f, 0.0f, true},
                     {0.035f, 0.08f}},
                    0.026f, 0.003f, {0.0f, -0.1f, -0.11f}, 14.0f);

    // A reflex sight: an open hood around a pane whose dot aiming puts on the crosshair.
    builder.Block(optic, {0.032f, 0.012f, 0.06f}, 0.002f, {0.0f, 0.059f, -0.06f});
    for (const float side : {-1.0f, 1.0f}) {
        builder.Block(optic, {0.006f, 0.042f, 0.05f}, 0.002f, {side * 0.0175f, 0.084f, -0.06f});
    }
    builder.Block(optic, {0.041f, 0.007f, 0.05f}, 0.002f, {0.0f, 0.1075f, -0.06f});
    builder.Block(optic, {0.008f, 0.008f, 0.012f}, 0.0015f, {0.024f, 0.084f, -0.045f});
    builder.Box(glass, {0.029f, 0.036f, 0.002f}, {0.0f, 0.085f, -0.075f});
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
    builder.Block(barrel, {0.008f, 0.0035f, 0.49f}, 0.001f, {0.0f, 0.0295f, -0.4f});
    builder.Turned(
        barrel,
        {{0.0f, 0.21f}, {0.011f, 0.21f}, {0.011f, -0.198f}, {0.0118f, -0.2f}, {0.0118f, -0.21f}, {0.0f, -0.21f}},
        {0.0f, -0.014f, -0.35f});
    builder.Block(receiver, {0.03f, 0.05f, 0.015f}, 0.003f, {0.0f, 0.001f, -0.56f});
    builder.Block(bead, {0.004f, 0.005f, 0.004f}, 0.001f, {0.0f, 0.031f, -0.645f});

    // The forend the player pumps after each shot, rounded around the magazine tube, with grooves for the fingers.
    const std::vector<ProfilePoint> forend = Ellipse(0.025f, 0.021f, 16);
    builder.Section(pump, forend, 0.17f, 0.008f, {0.0f, -0.014f, -0.33f});
    const std::vector<ProfilePoint> groove = Ellipse(0.0255f, 0.0215f, 16);
    for (int i = 0; i < 5; ++i) {
        builder.Section(grooves, groove, 0.004f, 0.001f, {0.0f, -0.014f, -0.27f - 0.03f * static_cast<float>(i)});
    }

    // The stock in one piece, from its wrist behind the trigger guard to its butt, which the pad caps.
    builder.Profile(stock,
                    {{0.075f, -0.035f},
                     {0.14f, -0.058f, true},
                     {0.25f, -0.082f, true},
                     {0.385f, -0.105f},
                     {0.385f, 0.012f},
                     {0.25f, 0.002f, true},
                     {0.14f, -0.004f, true},
                     {0.08f, 0.022f}},
                    0.044f, 0.01f, {});
    builder.Block(rubber, {0.046f, 0.121f, 0.02f}, 0.004f, {0.0f, -0.0465f, 0.394f});
    builder.Block(receiver, {0.01f, 0.004f, 0.06f}, 0.0012f, {0.0f, -0.045f, 0.02f});
    builder.Block(receiver, {0.008f, 0.016f, 0.004f}, 0.0012f, {0.0f, -0.037f, -0.008f});
    builder.Block(barrel, {0.005f, 0.02f, 0.006f}, 0.0015f, {0.0f, -0.035f, 0.02f}, 10.0f);

    builder.Marker("Muzzle", {0.0f, 0.016f, -0.665f});
    builder.Marker("Eye", {0.0f, 0.0335f, 0.27f});
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
        builder.AddMaterial(SurfaceMaterial("Launcher Band", Surface::PaintedMetal, {0.95f, 0.72f, 0.1f, 1.0f}));
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
    Builder builder;
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
    return models;
}

} // namespace Generator::Weapons
