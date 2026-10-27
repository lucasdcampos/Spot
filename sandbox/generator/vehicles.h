#pragma once

#include "gltf_writer.h"

// Drivable bodies, facing -Z with their origin on the ground between the wheels: body, glass, trim, interior and
// lights, then a shared wheel's "Tire" and "Rim", centered on the origin along X with the rim toward +X. Prefabs
// place and animate the wheels independently. Sizes are in meters.
namespace Generator::Vehicles {

GltfModel Car();
GltfModel Coupe();
// Cabin and skids in ground coordinates; MainRotor/MainTips and TailRotor/TailTips are centered on their pivots.
GltfModel Helicopter();

} // namespace Generator::Vehicles
