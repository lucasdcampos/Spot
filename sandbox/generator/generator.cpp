#include "generator.h"

#include "effect_textures.h"
#include "gltf_writer.h"
#include "icons.h"
#include "level_models.h"
#include "prop_textures.h"
#include "rigs.h"
#include "shapes.h"
#include "sounds.h"
#include "textures.h"
#include "vehicles.h"
#include "weapons.h"

#include <spot.h>
#include <spot/assets/primitive_meshes.h>

#include <functional>
#include <future>
#include <string>

namespace Generator {

namespace {

struct Prop {
    PropInfo info;
    std::function<Spot::MeshData()> mesh;
    // Paints the prop's own textures; none for a prop of a surface's.
    Textures::SurfaceTextures (*paint)(std::uint32_t size) = nullptr;
};

struct Texture {
    std::string_view name;
    std::function<Spot::Image()> paint;
};

// A crate's edges are beveled, so they catch the light; each face shows the whole texture, as a box's does.
Spot::MeshData CrateMesh() {
    Spot::MeshData mesh = Shapes::BeveledBox({1.0f, 1.0f, 1.0f}, 0.015f, 1.0f);
    for (Spot::MeshVertex& vertex : mesh.vertices) {
        vertex.uv = vertex.uv + Spot::Vector2{0.5f, 0.5f};
    }
    return mesh;
}

// Sizes are in meters, as the player is about 1.8 tall.
std::vector<Prop> MakeProps() {
    return {
        {{"crate", "crate"}, CrateMesh, Textures::Crate},
        {{"barrel", "barrel"}, [] { return Shapes::Drum(0.3f, 0.9f, 48); }, Textures::Barrel},
        {{"explosive_barrel", "explosive_barrel"},
         [] { return Shapes::Drum(0.3f, 0.9f, 48); },
         Textures::ExplosiveBarrel},
        {{"plank", "plank"}, [] { return Shapes::Box({2.0f, 0.06f, 0.3f}, 1.0f); }, Textures::Plank},
        {{"beam", "beam"}, [] { return Shapes::Box({3.0f, 0.2f, 0.2f}, 0.75f); }, Textures::Beam},
        {{"ball", "ball"}, [] { return Spot::PrimitiveMeshes::CreateSphere(0.3f, 48, 24); }, Textures::Ball},
        {{"cone", "cone"}, [] { return Shapes::TrafficCone(0.3f, 0.7f, 0.64f, 0.03f, 48); }, Textures::Cone},
        {{"ramp", "concrete"}, [] { return Shapes::Ramp(2.0f, 1.0f, 3.0f, 1.0f); }},
        {{"stairs", "concrete"}, [] { return Shapes::Stairs(2.0f, 0.2f, 0.3f, 10, 1.0f); }},
        {{"wheel", "wheel"}, [] { return Shapes::Wheel(0.4f, 0.2f, 48); }, Textures::Wheel},
        {{"target_dummy", "target_dummy"}, Shapes::TargetDummy, Textures::TargetDummy},
    };
}

const std::vector<Texture>& GetTextures() {
    static const std::vector<Texture> textures = {
        {"ui_panel", Textures::UiPanel},
        {"ui_outline", Textures::UiOutline},
        {"ui_shadow", Textures::UiShadow},
        {"ui_scrim", Textures::UiScrim},
        {"ui_dot", Textures::UiDot},
        {"ui_hit_marker", Textures::UiHitMarker},
        {"chain_link", Textures::ChainLink},
        {"ui_wheel_sector", Icons::WheelSector},
        {"ui_vignette", Icons::Vignette},
        {"icon_physics_gun", Icons::PhysicsGun},
        {"icon_pistol", Icons::Pistol},
        {"icon_rifle", Icons::Rifle},
        {"icon_shotgun", Icons::Shotgun},
        {"icon_rocket_launcher", Icons::RocketLauncher},
        {"icon_tool_gun", Icons::ToolGun},
        {"icon_sun", Icons::Sun},
        {"icon_moon", Icons::Moon},
        {"icon_health", Icons::Health},
        {"icon_search", Icons::Search},
        {"icon_view_grid", Icons::ViewGrid},
        {"icon_view_compact", Icons::ViewCompact},
        {"icon_category_props", Icons::CategoryProps},
        {"icon_category_lamps", Icons::CategoryLamps},
        {"icon_category_weapons", Icons::CategoryWeapons},
        {"icon_category_npcs", Icons::CategoryNpcs},
        {"fx_smoke", EffectTextures::Smoke},
        {"fx_fire", EffectTextures::Fire},
        {"fx_muzzle_flash", EffectTextures::MuzzleFlash},
        {"fx_flame", EffectTextures::Flame},
        {"fx_debris", EffectTextures::Debris},
        {"fx_ripple", EffectTextures::Ripple},
        {"fx_blood", EffectTextures::BloodSplat},
        {"fx_blood_spatter", EffectTextures::BloodSpatter},
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
        {"splash_small", Sounds::SplashSmall},
        {"splash_large", Sounds::SplashLarge},
        {"reload", Sounds::Reload},
        {"impact_concrete", Sounds::ImpactConcrete},
        {"impact_wood", Sounds::ImpactWood},
        {"impact_metal", Sounds::ImpactMetal},
        {"footstep_concrete_1", [] { return Sounds::FootstepConcrete(0); }},
        {"footstep_concrete_2", [] { return Sounds::FootstepConcrete(1); }},
        {"footstep_concrete_3", [] { return Sounds::FootstepConcrete(2); }},
        {"footstep_concrete_4", [] { return Sounds::FootstepConcrete(3); }},
        {"footstep_wood_1", [] { return Sounds::FootstepWood(0); }},
        {"footstep_wood_2", [] { return Sounds::FootstepWood(1); }},
        {"footstep_wood_3", [] { return Sounds::FootstepWood(2); }},
        {"footstep_wood_4", [] { return Sounds::FootstepWood(3); }},
        {"footstep_metal_1", [] { return Sounds::FootstepMetal(0); }},
        {"footstep_metal_2", [] { return Sounds::FootstepMetal(1); }},
        {"footstep_metal_3", [] { return Sounds::FootstepMetal(2); }},
        {"footstep_metal_4", [] { return Sounds::FootstepMetal(3); }},
        {"physics_gun_hum", Sounds::PhysicsGunHum},
        {"engine", Sounds::Engine},
        {"helicopter_rotor", Sounds::HelicopterRotor},
        {"tire_squeal", Sounds::TireSqueal},
        {"horn", Sounds::Horn},
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

std::vector<std::string_view> GetLevelModelNames() {
    std::vector<std::string_view> names;
    for (const LevelModels::LevelModel& level : LevelModels::MakeLevelModels()) {
        names.push_back(level.name);
    }
    return names;
}

std::vector<std::string_view> GetRigModelNames() {
    return {"humanoid"};
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

OutputCounts GenerateSandboxAssets(const std::filesystem::path& directory, std::uint32_t textureDivisor) {
    OutputCounts counts;
    // Painting and encoding images is most of the work, and each is on its own, so they are painted on threads of
    // their own and written in order as they are done.
    struct PaintedImage {
        std::string name;
        std::future<std::vector<std::byte>> png;
    };
    std::vector<PaintedImage> images;
    for (const Texture& texture : GetTextures()) {
        images.push_back({std::string(texture.name),
                          std::async(std::launch::async, [&texture] { return texture.paint().EncodePng(); })});
    }
    const auto paintSet = [&](const std::string& name, std::function<Textures::SurfaceTextures()> paint) {
        auto textures = std::async(std::launch::async, std::move(paint)).share();
        images.push_back(
            {name + "_color", std::async(std::launch::async, [textures] { return textures.get().color.EncodePng(); })});
        images.push_back({name + "_normal",
                          std::async(std::launch::async, [textures] { return textures.get().normal.EncodePng(); })});
        images.push_back(
            {name + "_orm", std::async(std::launch::async, [textures] { return textures.get().orm.EncodePng(); })});
    };
    for (const Textures::Surface surface : Textures::Surfaces) {
        paintSet(std::string(Textures::GetSurfaceName(surface)), [surface, textureDivisor] {
            return Textures::PaintSurface(surface, Textures::GetSurfaceSize(surface) / textureDivisor);
        });
    }
    for (const Prop& prop : MakeProps()) {
        if (prop.paint) {
            paintSet(std::string(prop.info.textures),
                     [paint = prop.paint, textureDivisor] { return paint(Textures::PropSize / textureDivisor); });
        }
    }
    for (PaintedImage& image : images) {
        WriteIfChanged(directory / "textures" / (image.name + ".png"), image.png.get(), counts);
    }
    const auto writeModel = [&](const std::string& name, const GltfModel& model) {
        const GltfFiles files = EncodeGltf(model, name + ".bin");
        WriteIfChanged(directory / "models" / (name + ".gltf"), files.json, counts);
        WriteIfChanged(directory / "models" / (name + ".bin"), files.buffer, counts);
    };
    for (const Prop& prop : MakeProps()) {
        const std::string name(prop.info.name);
        const std::string textures = "../textures/" + std::string(prop.info.textures);
        GltfModel model;
        model.materials.push_back({.name = name,
                                   .baseColorImage = textures + "_color.png",
                                   .metallic = 1.0f,
                                   .roughness = 1.0f,
                                   .normalImage = textures + "_normal.png",
                                   .ormImage = textures + "_orm.png"});
        model.parts.push_back({.name = name, .mesh = prop.mesh(), .material = 0});
        writeModel(name, model);
    }
    for (const Weapons::WeaponModel& weapon : Weapons::MakeWeaponModels()) {
        writeModel(std::string(weapon.name), weapon.model);
    }
    for (const LevelModels::LevelModel& level : LevelModels::MakeLevelModels()) {
        writeModel(std::string(level.name), level.model);
    }
    writeModel("humanoid", Rigs::Humanoid());
    writeModel("car", Vehicles::Car());
    writeModel("coupe", Vehicles::Coupe());
    writeModel("helicopter", Vehicles::Helicopter());
    for (const GeneratedSound& sound : GetSounds()) {
        const std::vector<std::byte> wav = Sounds::EncodeWav(sound.make());
        WriteIfChanged(directory / "sounds" / (std::string(sound.name) + ".wav"), wav, counts);
    }
    return counts;
}

} // namespace Generator
