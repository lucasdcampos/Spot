#pragma once

#include "spot/core/math/angles.h"
#include "spot/core/math/color.h"
#include "spot/core/math/vector.h"

namespace Spot {

// A layer of clouds high above the viewer, which the sky draws over itself, lit as the sky is: white by day, glowing
// red at sunset, dark gray against the stars at night. Its shapes are noise, so they need no texture, and moving
// offset makes them drift.
struct SkyClouds {
    // How much of the sky they cover, from 0, a clear sky, to 1, overcast.
    float coverage = 0.0f;
    // How opaque their thick parts are, from 0 to 1.
    float density = 0.9f;
    // How high the layer is above the viewer, and about how wide a cloud is, in meters.
    float height = 1500.0f;
    float size = 900.0f;
    // How blurred their edges are, from 0, crisp, to 1, wispy.
    float softness = 0.35f;
    // How far the wind has carried them, in meters along X and Z.
    Vector2 offset = {};
    // Multiplies the light they get, white by default.
    Color color = Color::White;

    bool operator==(const SkyClouds&) const = default;
};

// What the sky shows once the sun has set: stars, which turn with the sky and twinkle, and a moon opposite the sun,
// which rises as it sets. Both fade in as the sun goes down and are hidden by clouds.
struct SkyNight {
    // How bright the stars are; 0 hides them.
    float starBrightness = 1.0f;
    // How many there are, from 0 to 1.
    float starDensity = 0.5f;
    // The axis the night sky turns around, and how far it has turned, in radians: a sun crossing from east (+X) to
    // west turns around Z, and a whole day is a whole turn.
    Vector3 starAxis = {0.0f, 0.0f, 1.0f};
    float starRotation = 0.0f;
    // Seconds that drive the stars' twinkling.
    float time = 0.0f;
    // The moon's angular radius, in radians, and its brightness; a size of 0 hides it.
    float moonSize = ToRadians(1.6f);
    float moonBrightness = 1.0f;

    bool operator==(const SkyNight&) const = default;
};

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
    // The angular radius of the sun's bright core, in radians, which its glow spreads well beyond; 0 hides the sun.
    // The real sun's is about a quarter of a degree, but it is so bright that it looks far larger.
    float sunSize = ToRadians(2.5f);
    // How much air the light crosses: more makes the sky hazier and paler and its sunsets redder.
    float atmosphereThickness = 1.0f;
    // Multiplies the color the air scatters, the blue of a clear sky.
    Color skyTint = Color::White;
    // The color of the ground seen below the horizon, which the sun and sky light like a surface of the scene, seen
    // through the haze near the horizon: a neutral gray by default.
    Color groundColor = {0.2f, 0.2f, 0.21f};
    // Faint light the sky keeps where sunlight does not reach it, as from the moon and stars, so nights are not
    // pitch black. It also lights the scene.
    Color nightColor = {0.004f, 0.006f, 0.014f};
    // Brightens or darkens the whole sky.
    float exposure = 1.0f;
    // Whether the sky lights the scene, with its sun and its ambient light, or is only drawn.
    bool lightsScene = true;
    // Scales the light the sky casts on the scene, diffuse and reflected; 0 leaves only the sun.
    float ambientIntensity = 1.0f;
    // Whether the sun casts shadows when it lights the scene; see LightData::castsShadows.
    bool sunCastsShadows = true;
    // Drawn only, without changing how the sky lights the scene.
    SkyClouds clouds = {};
    SkyNight night = {};

    bool operator==(const SkyData&) const = default;
};

} // namespace Spot
