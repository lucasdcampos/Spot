// What the sky's clouds are made of and lit by, which clouds.frag.hlsl draws as a layer and clouds_volume.frag.hlsl as
// a volume. Include sky.hlsl first.

#include "cloud_shapes.hlsl"

cbuffer Clouds : register(b2, space3) {
    // Of the camera's rotation and projection only, which takes directions from the camera to clip space.
    float4x4 viewProjection;
    // The camera's position, and the layer's altitude in w.
    float4 view;
    // The clouds' coverage, density and softness, and about how wide one is, in meters.
    float4 cloudShape;
    // How far the wind has carried them along X and Z, in meters, and how many clouds wide the noise's texture is.
    float4 cloudMotion;
    float4 cloudColor;
    // The sky's light from above, which lights the clouds' shaded sides as it lights the ground.
    float4 skylight;
    // How tall the layer is from its altitude up, in meters, for clouds drawn as a volume, and the frame, which turns
    // the noise their rays start with.
    float4 volume;
};

static const float PlanetRadiusMeters = PlanetRadius * 1000.0;
// How much of the moon's light, before the air, reaches the clouds.
static const float MoonlightOnClouds = 0.6;
// Clouds fade into the haze over this many meters, for an atmosphere of thickness 1.
static const float HazeDistance = 40000.0;

// The share of the light falling on a cloud of the optical depth that goes through it, straight or scattered; the rest
// comes back out the side it fell on (a two-stream approximation, for the forward scattering of water droplets).
float GetDiffuseTransmittance(float opticalDepth) {
    return 1.0 / (1.0 + 0.1125 * opticalDepth);
}

// Henyey-Greenstein, relative to scattering evenly in all directions.
float GetPhase(float cosAngle, float g) {
    const float g2 = g * g;
    return (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * cosAngle, 1e-4), 1.5);
}

// The light that lights the clouds: by day the sun, and by night the moon opposite it, both from above. None without a
// sun.
float3 GetCloudLight(out float3 toLight) {
    const float3 toSun = sunDirection.xyz;
    const bool day = toSun.y >= 0.0;
    toLight = day ? toSun : -toSun;
    return sunDirection.w > 0.5 ? (day ? sunLight.rgb : moonlight.rgb * MoonlightOnClouds) : 0.0;
}
