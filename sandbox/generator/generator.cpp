#include "generator.h"

#include "gltf_writer.h"
#include "rigs.h"
#include "shapes.h"
#include "sounds.h"
#include "textures.h"
#include "weapons.h"

#include <spot.h>

#include <functional>
#include <string>

namespace Generator {

namespace {

struct Prop {
    PropInfo info;
    std::function<Spot::MeshData()> mesh;
    // As in glTF, multiplying the ORM texture's channels.
    float metallic = 1.0f;
    float roughness = 1.0f;
};

struct Texture {
    std::string_view name;
    std::function<Spot::Image()> paint;
};

// Sizes are in meters, as the player is about 1.8 tall.
std::vector<Prop> MakeProps() {
    return {
        // Their colors are painted for their shapes; their surfaces' maps add relief, roughness and metal.
        {{"crate", "crate", "wood"}, [] { return Shapes::Box({1.0f, 1.0f, 1.0f}, 1.0f); }},
        {{"barrel", "barrel", "painted_metal"}, [] { return Shapes::Cylinder(0.3f, 0.9f, 24); }},
        {{"explosive_barrel", "explosive_barrel", "painted_metal"}, [] { return Shapes::Cylinder(0.3f, 0.9f, 24); }},
        {{"plank", "plank", "wood"}, [] { return Shapes::Box({2.0f, 0.06f, 0.3f}, 1.0f); }},
        {{"beam", "beam", "rusty_metal"}, [] { return Shapes::Box({3.0f, 0.2f, 0.2f}, 0.75f); }},
        {{"ball", "ball", "rubber"}, [] { return Spot::MeshData::CreateSphere(0.3f, 32, 16); }, 0.0f, 0.7f},
        {{"cone", "cone", "rubber"}, [] { return Shapes::Cone(0.3f, 0.7f, 24); }},
        {{"ramp", "concrete", "concrete"}, [] { return Shapes::Ramp(2.0f, 1.0f, 3.0f, 1.0f); }},
        {{"stairs", "concrete", "concrete"}, [] { return Shapes::Stairs(2.0f, 0.2f, 0.3f, 10, 1.0f); }},
        {{"wheel", "wheel", "rubber"}, [] { return Shapes::Wheel(0.4f, 0.2f, 32); }},
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

struct GeneratedSound {
    std::string_view name;
    std::function<Sounds::Samples()> make;
};

const std::vector<GeneratedSound>& GetSounds() {
    static const std::vector<GeneratedSound> sounds = {
        {"pistol_shot", Sounds::PistolShot},
        {"rifle_shot", Sounds::RifleShot},
        {"shotgun_shot", Sounds::ShotgunShot},
        {"rocket_launch", Sounds::RocketLaunch},
        {"explosion", Sounds::Explosion},
        {"reload", Sounds::Reload},
        {"impact_concrete", Sounds::ImpactConcrete},
        {"impact_wood", Sounds::ImpactWood},
        {"impact_metal", Sounds::ImpactMetal},
        {"footstep_concrete_1", [] { return Sounds::FootstepConcrete(0); }},
        {"footstep_concrete_2", [] { return Sounds::FootstepConcrete(1); }},
        {"footstep_wood_1", [] { return Sounds::FootstepWood(0); }},
        {"footstep_wood_2", [] { return Sounds::FootstepWood(1); }},
        {"footstep_metal_1", [] { return Sounds::FootstepMetal(0); }},
        {"footstep_metal_2", [] { return Sounds::FootstepMetal(1); }},
        {"physics_gun_hum", Sounds::PhysicsGunHum},
        {"wind", Sounds::Wind},
        {"music", Sounds::Music},
        {"click", Sounds::Click},
    };
    return sounds;
}

} // namespace

std::vector<std::string_view> GetSoundNames() {
    std::vector<std::string_view> names;
    for (const GeneratedSound& sound : GetSounds()) {
        names.push_back(sound.name);
    }
    return names;
}

std::vector<std::string_view> GetRigModelNames() {
    return {"robot", "arms"};
}

std::vector<std::string_view> GetWeaponModelNames() {
    std::vector<std::string_view> names;
    for (const Weapons::WeaponModel& weapon : Weapons::MakeWeaponModels()) {
        names.push_back(weapon.name);
    }
    return names;
}

std::vector<std::string_view> GetSurfaceNames() {
    std::vector<std::string_view> names;
    for (const Textures::Surface surface : Textures::Surfaces) {
        names.push_back(Textures::GetSurfaceName(surface));
    }
    return names;
}

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
    for (const Textures::Surface surface : Textures::Surfaces) {
        const std::string name(Textures::GetSurfaceName(surface));
        const Textures::SurfaceTextures textures = Textures::PaintSurface(surface);
        WriteIfChanged(directory / "textures" / (name + "_color.png"), textures.color.EncodePng(), counts);
        WriteIfChanged(directory / "textures" / (name + "_normal.png"), textures.normal.EncodePng(), counts);
        WriteIfChanged(directory / "textures" / (name + "_orm.png"), textures.orm.EncodePng(), counts);
    }
    const auto writeModel = [&](const std::string& name, const GltfModel& model) {
        const GltfFiles files = EncodeGltf(model, name + ".bin");
        WriteIfChanged(directory / "models" / (name + ".gltf"), files.json, counts);
        WriteIfChanged(directory / "models" / (name + ".bin"), files.buffer, counts);
    };
    for (const Prop& prop : MakeProps()) {
        const std::string name(prop.info.name);
        const std::string surface = "../textures/" + std::string(prop.info.surface);
        GltfModel model;
        model.materials.push_back({.name = name,
                                   .baseColorImage = "../textures/" + std::string(prop.info.texture) + ".png",
                                   .metallic = prop.metallic,
                                   .roughness = prop.roughness,
                                   .normalImage = surface + "_normal.png",
                                   .ormImage = surface + "_orm.png"});
        model.parts.push_back({.name = name, .mesh = prop.mesh(), .material = 0});
        writeModel(name, model);
    }
    for (const Weapons::WeaponModel& weapon : Weapons::MakeWeaponModels()) {
        writeModel(std::string(weapon.name), weapon.model);
    }
    writeModel("robot", Rigs::Robot());
    writeModel("arms", Rigs::Arms());
    for (const GeneratedSound& sound : GetSounds()) {
        const std::vector<std::byte> wav = Sounds::EncodeWav(sound.make());
        WriteIfChanged(directory / "sounds" / (std::string(sound.name) + ".wav"), wav, counts);
    }
    return counts;
}

} // namespace Generator
