#pragma once

#include "spot/core/math/vector.h"

#include <cstdint>

namespace Spot {

// A reflection probe for Renderer3D::DrawReflectionProbe(): the scene captured all around a point, which lights what
// is inside its box in place of the sky, scattering and reflecting the light around it, as rooms and caves need,
// where the sky does not reach and light bounces off the walls instead. Renderer3D captures it the first time it is
// drawn, and again when it moves or its version changes; between, it costs about as much as a light.
struct ReflectionProbeData {
    // Tells the probe apart from the others from frame to frame, so its capture is kept: any value unique among the
    // probes drawn.
    std::uint64_t id = 0;
    // Where it captures the scene from, inside its box.
    Vector3 position = {};
    // The box it lights, along the world's axes. Reflections are projected onto it, as onto the walls of the room it
    // stands for, so they line up with what they reflect.
    Vector3 boxMin = {-5.0f, -5.0f, -5.0f};
    Vector3 boxMax = {5.0f, 5.0f, 5.0f};
    // How far inside the box its light fades in from the light around it. Where boxes overlap, the smaller lights
    // what it holds.
    float blendDistance = 1.0f;
    // Scales its light.
    float intensity = 1.0f;
    // Changing it captures the scene again, for when what is around the probe changes, such as the time of day.
    std::uint32_t version = 0;
};

} // namespace Spot
