#pragma once

#include "gltf_writer.h"

// The car the player drives, a hatchback: its body, glass, trim, interior and lights, facing -Z with its origin on the
// ground between its wheels, and one wheel, its "Tire" and "Rim", centered on the origin, lying along X with the rim
// toward +X, for each of the car's wheel entities. Sizes are in meters.
namespace Generator::Vehicles {

GltfModel Car();

} // namespace Generator::Vehicles
