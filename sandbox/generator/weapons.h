#pragma once

#include "gltf_writer.h"

#include <string_view>
#include <vector>

// The low-poly models of what the player holds, made of boxes and cylinders with the surfaces' textures. They point
// down -Z with +Y up, as held in front of the eyes, the grip near the origin. Sizes are in meters.
namespace Generator::Weapons {

struct WeaponModel {
    std::string_view name;
    GltfModel model;
};

// Separate forearms and gripping hands for the first-person presentation.
GltfModel MakeFirstPersonArms();

// The pistol, rifle, shotgun, rocket launcher, tool gun and physics gun.
std::vector<WeaponModel> MakeWeaponModels();

} // namespace Generator::Weapons
