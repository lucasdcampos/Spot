#include "generator.h"

#include "gltf_writer.h"
#include "shapes.h"
#include "textures.h"

#include <spot.h>

#include <functional>
#include <string>

namespace Generator {

namespace {

struct Prop {
    PropInfo info;
    std::function<Spot::MeshData()> mesh;
};

struct Texture {
    std::string_view name;
    std::function<Spot::Image()> paint;
};

// Sizes are in meters, as the player is about 1.8 tall.
std::vector<Prop> MakeProps() {
    return {
        {{"crate", "crate"}, [] { return Shapes::Box({1.0f, 1.0f, 1.0f}, 1.0f); }},
        {{"barrel", "barrel"}, [] { return Shapes::Cylinder(0.3f, 0.9f, 24); }},
        {{"explosive_barrel", "explosive_barrel"}, [] { return Shapes::Cylinder(0.3f, 0.9f, 24); }},
        {{"plank", "plank"}, [] { return Shapes::Box({2.0f, 0.06f, 0.3f}, 1.0f); }},
        {{"beam", "beam"}, [] { return Shapes::Box({3.0f, 0.2f, 0.2f}, 0.75f); }},
        {{"ball", "ball"}, [] { return Spot::MeshData::CreateSphere(0.3f, 32, 16); }},
        {{"cone", "cone"}, [] { return Shapes::Cone(0.3f, 0.7f, 24); }},
        {{"ramp", "concrete"}, [] { return Shapes::Ramp(2.0f, 1.0f, 3.0f, 1.0f); }},
        {{"stairs", "concrete"}, [] { return Shapes::Stairs(2.0f, 0.2f, 0.3f, 10, 1.0f); }},
        {{"wheel", "wheel"}, [] { return Shapes::Wheel(0.4f, 0.2f, 32); }},
    };
}

const std::vector<Texture>& GetTextures() {
    static const std::vector<Texture> textures = {
        {"crate", Textures::Crate}, {"barrel", Textures::Barrel},     {"explosive_barrel", Textures::ExplosiveBarrel},
        {"plank", Textures::Plank}, {"beam", Textures::Beam},         {"ball", Textures::Ball},
        {"cone", Textures::Cone},   {"concrete", Textures::Concrete}, {"wheel", Textures::Wheel},
    };
    return textures;
}

} // namespace

std::vector<PropInfo> GetProps() {
    std::vector<PropInfo> props;
    for (const Prop& prop : MakeProps()) {
        props.push_back(prop.info);
    }
    return props;
}

OutputCounts GenerateSandboxAssets(const std::filesystem::path& directory) {
    OutputCounts counts;
    for (const Texture& texture : GetTextures()) {
        const std::vector<std::byte> png = texture.paint().EncodePng();
        WriteIfChanged(directory / "textures" / (std::string(texture.name) + ".png"), png, counts);
    }
    for (const Prop& prop : MakeProps()) {
        const std::string name(prop.info.name);
        GltfModel model;
        model.materials.push_back(
            {.name = name, .baseColorImage = "../textures/" + std::string(prop.info.texture) + ".png"});
        model.parts.push_back({.name = name, .mesh = prop.mesh(), .material = 0});
        const GltfFiles files = EncodeGltf(model, name + ".bin");
        WriteIfChanged(directory / "models" / (name + ".gltf"), files.json, counts);
        WriteIfChanged(directory / "models" / (name + ".bin"), files.buffer, counts);
    }
    return counts;
}

} // namespace Generator
