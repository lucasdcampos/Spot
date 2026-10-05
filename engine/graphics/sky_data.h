#pragma once

#include "graphics/color.h"
#include "math/angles.h"
#include "math/vector.h"

namespace Spot {

// A sky for Renderer3D::DrawSky(): an atmosphere lit by the sun, which it scatters into a blue sky by day and red
// sunsets, with the sun's disc in it and the ground below the horizon. Colors are linear.
struct SkyData {
    // Without a sun, nothing lights the atmosphere and the sky is black, as at night.
    bool hasSun = true;
    // The direction sunlight travels in, like LightData::direction. As the sun nears the horizon, its light crosses
    // more air and the sky reddens; below it, the sky darkens.
    Vector3 sunDirection = {-0.4f, -1.0f, -0.6f};
    Color sunColor = Color::White;
    float sunIntensity = 1.0f;
    // The angular radius of the sun's disc, in radians; 0 hides the disc. The real sun's is about a quarter of a
    // degree, which looks too small on a screen.
    float sunSize = ToRadians(0.75f);
    // How much air the light crosses: more makes the sky hazier and paler and its sunsets redder.
    float atmosphereThickness = 1.0f;
    // Multiplies the color the air scatters, the blue of a clear sky.
    Color skyTint = Color::White;
    // The color of the ground seen below the horizon, which the sun and sky light like a surface of the scene: an
    // earthy brown by default.
    Color groundColor = {0.11f, 0.09f, 0.07f};
    // Faint light the sky keeps where sunlight does not reach it, as from the moon and stars, so nights are not
    // pitch black. It also lights the scene.
    Color nightColor = {0.004f, 0.006f, 0.014f};
    // Brightens or darkens the whole sky.
    float exposure = 1.0f;
    // Whether the sky lights the scene, with its sun and its ambient light, or is only drawn.
    bool lightsScene = true;
    // Scales the ambient light the sky casts on the scene; 0 leaves only the sun.
    float ambientIntensity = 1.0f;
};

} // namespace Spot
