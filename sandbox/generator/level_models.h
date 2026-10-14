#pragma once

#include "gltf_writer.h"

#include <string_view>
#include <vector>

// The playground's scenery: models the level is dressed with rather than props to spawn. Sizes are in meters, each
// standing on its origin unless said otherwise.
namespace Generator::LevelModels {

struct LevelModel {
    std::string_view name;
    GltfModel model;
};

// The ground beyond the paved yards: flat where the level is, rising to hills around it, with a pond's hollow. Its
// "Grass" part covers the gentle ground and its "Rock" part the steep slopes.
GltfModel Terrain();
// The terrain's height at a point, so the level can stand things on it.
float GetTerrainHeight(float x, float z);

// Every model, in the order they are written: the terrain, rocks, trees, a shipping container, a street lamp, a
// concrete barrier, a bench, a quarter pipe, a fence panel, the forest on the hills, the road's markings and the neon
// signs.
std::vector<LevelModel> MakeLevelModels();

} // namespace Generator::LevelModels
