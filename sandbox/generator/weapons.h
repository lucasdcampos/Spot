#pragma once

#include "gltf_writer.h"

#include <string_view>
#include <vector>

// Detailed models of what the player holds, with radiused machined edges and tiling PBR surfaces. They point
// down -Z with +Y up, as held in front of the eyes, the grip near the origin. Sizes are in meters.
namespace Generator::Weapons {

struct WeaponModel {
    std::string_view name;
    GltfModel model;
};

// Separate forearms and gripping hands for the first-person presentation.
GltfModel MakeFirstPersonArms();

// The pistol, rifle, shotgun, rocket launcher, tool gun, physics gun and developer ray gun.
std::vector<WeaponModel> MakeWeaponModels();

} // namespace Generator::Weapons
