cbuffer Sky : register(b0, space3) {
    // The direction toward the sun, and 1 in w when there is a sun.
    float4 sunDirection;
    // Premultiplied by the sun's intensity.
    float4 sunColor;
    float4 skyTint;
    float4 groundColor;
    // The light that reaches the ground, from the sun through the air and from the sky, like a surface of the scene.
    float4 groundLight;
    float4 nightColor;
    // The sun's angular radius in radians, the atmosphere's thickness and the exposure.
    float4 settings;
};

struct Input {
    float4 position : SV_Position;
    float4 direction : TEXCOORD0;
};

// Single scattering in an Earth-like atmosphere (Nishita et al. 1993, with Bruneton's 2017 coefficients), in
// kilometers. The viewer stands a little above a planet's surface. graphics/atmosphere.cpp computes the same on the
// CPU to light the scene, so the constants must match.
static const float PlanetRadius = 6360.0;
static const float AtmosphereRadius = 6420.0;
static const float ViewerHeight = 0.05;
// The heights over which the density of air molecules (Rayleigh) and of haze (Mie) falls by a factor of e.
static const float RayleighHeight = 8.0;
static const float MieHeight = 1.2;
// Per kilometer at sea level. Air scatters blue the most, which colors the sky; haze scatters all colors alike,
// mostly forward, which makes the glow around the sun.
static const float3 RayleighScattering = float3(5.802e-3, 13.558e-3, 33.1e-3);
static const float MieScattering = 3.996e-3;
static const float MieExtinction = 4.44e-3;
static const float MieAnisotropy = 0.8;
// How bright the sky is drawn for a sun of intensity 1, before the exposure.
static const float SkyBrightness = 40.0;
// How bright the sun's disc is drawn: far brighter than the sky, as the real sun is, so it stands out from its glow,
// yet low enough that it turns orange once the air near the horizon takes most of its blue and green.
static const float SunDiscBrightness = 400.0;
// How far below the horizon, as the cosine of the angle from level, the ground fades in from the haze.
static const float HorizonFade = 0.02;
static const float Pi = 3.14159265;

#define VIEW_SAMPLES 16
#define SUN_SAMPLES 8

// The distance to where a ray leaves the sphere of the radius, from a point inside it at the radius r from the
// planet's center, going in a direction whose cosine with straight up is mu.
float GetExitDistance(float r, float mu, float sphereRadius) {
    const float b = r * mu;
    // Factored, because r * r - sphereRadius * sphereRadius loses the difference to rounding.
    const float c = (r - sphereRadius) * (r + sphereRadius);
    return -b + sqrt(max(b * b - c, 0.0));
}

// The distance to where the ray hits the ground, or a negative number when it misses.
float GetGroundDistance(float r, float mu) {
    const float b = r * mu;
    const float c = (r - PlanetRadius) * (r + PlanetRadius);
    const float discriminant = b * b - c;
    if (mu >= 0.0 || discriminant < 0.0) {
        return -1.0;
    }
    // The nearer root, written so it does not cancel out for viewers close to the ground.
    return c / (-b + sqrt(discriminant));
}

float2 GetDensity(float r) {
    return exp(-(r - PlanetRadius) / float2(RayleighHeight, MieHeight));
}

// The amount of air and haze, weighted by density, between a point and the sun; false when the planet hides the sun.
bool GetSunOpticalDepth(float r, float muSun, out float2 depth) {
    depth = 0.0;
    if (GetGroundDistance(r, muSun) > 0.0) {
        return false;
    }
    const float step = GetExitDistance(r, muSun, AtmosphereRadius) / SUN_SAMPLES;
    for (int i = 0; i < SUN_SAMPLES; ++i) {
        const float s = (i + 0.5) * step;
        depth += GetDensity(sqrt(r * r + s * s + 2.0 * r * s * muSun)) * step;
    }
    return true;
}

float3 GetTransmittance(float2 depth) {
    const float thickness = settings.y;
    return exp(-(RayleighScattering * depth.x + MieExtinction * depth.y) * thickness);
}

// The sunlight the air scatters toward the viewer from a direction at or above the horizon, for a sun of irradiance
// 1, and how much of what lies beyond the air gets through it.
float3 GetSkyRadiance(float3 direction, float3 toSun, out float3 transmittance) {
    const float r0 = PlanetRadius + ViewerHeight;
    const float mu = max(direction.y, 0.0);
    const float muSun = toSun.y;
    const float nu = dot(direction, toSun);
    const float thickness = settings.y;
    const float distance = GetExitDistance(r0, mu, AtmosphereRadius);

    // Rays along the ground cross hundreds of kilometers of dense air, so the steps start short near the viewer, and
    // each point is dimmed only by the air up to it, not by its whole step.
    float2 viewDepth = 0.0;
    float3 rayleighSum = 0.0;
    float3 mieSum = 0.0;
    for (int i = 0; i < VIEW_SAMPLES; ++i) {
        const float start = distance * pow(float(i) / VIEW_SAMPLES, 2.0);
        const float end = distance * pow(float(i + 1) / VIEW_SAMPLES, 2.0);
        const float t = (start + end) * 0.5;
        const float r = sqrt(r0 * r0 + t * t + 2.0 * r0 * t * mu);
        const float2 density = GetDensity(r) * (end - start);
        float2 sunDepth;
        if (GetSunOpticalDepth(r, (r0 * muSun + t * nu) / r, sunDepth)) {
            const float3 pathTransmittance = GetTransmittance(viewDepth + density * 0.5 + sunDepth);
            rayleighSum += density.x * pathTransmittance;
            mieSum += density.y * pathTransmittance;
        }
        viewDepth += density;
    }
    transmittance = GetTransmittance(viewDepth);

    const float rayleighPhase = 3.0 / (16.0 * Pi) * (1.0 + nu * nu);
    const float g2 = MieAnisotropy * MieAnisotropy;
    const float miePhase = 3.0 / (8.0 * Pi) * (1.0 - g2) * (1.0 + nu * nu) /
                           ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * MieAnisotropy * nu, 1e-4), 1.5));
    return (rayleighSum * RayleighScattering * rayleighPhase * skyTint.rgb + mieSum * MieScattering * miePhase) *
           thickness;
}

// The light of the sky toward the direction, as drawn before the exposure.
float3 GetSkyLight(float3 direction, float3 toSun, out float3 transmittance) {
    const float3 sunlight = sunColor.rgb * sunDirection.w * SkyBrightness;
    return GetSkyRadiance(direction, toSun, transmittance) * sunlight + nightColor.rgb;
}

// Maps unbounded light to what the target can show, keeping bright skies from clipping to white.
float3 Expose(float3 light) {
    return 1.0 - exp(-light * settings.z);
}

float4 main(Input input) : SV_Target0 {
    const float3 direction = normalize(input.direction.xyz / input.direction.w);
    const float3 toSun = sunDirection.xyz;
    float3 transmittance;
    float3 color;
    // How many sun radii the direction is from the sun's center. Its rate across the screen, which smooths the disc's
    // edge, is only defined outside of branches.
    const float sunRadius = settings.x;
    const float sunDistance = length(direction - toSun) / max(sunRadius, 1e-6);
    const float sunEdge = max(fwidth(sunDistance), 1e-4);

    if (direction.y >= 0.0) {
        float3 light = GetSkyLight(direction, toSun, transmittance);

        // The sun's disc, darker toward its rim as the real sun is, with an edge smoothed over about a pixel.
        if (sunDirection.w > 0.5 && sunRadius > 0.0) {
            const float coverage = saturate((1.0 - sunDistance) / sunEdge + 0.5);
            const float rim = sqrt(saturate(1.0 - sunDistance * sunDistance));
            light += sunColor.rgb * transmittance * SunDiscBrightness * (0.4 + 0.6 * rim) * coverage;
        }
        color = Expose(light);
    } else {
        // The ground fades in below the horizon out of the haze there, so the two meet without a line.
        const float3 level = float3(direction.x, 0.0, direction.z);
        const float3 horizon = dot(level, level) > 1e-8 ? normalize(level) : float3(1.0, 0.0, 0.0);
        const float3 haze = Expose(GetSkyLight(horizon, toSun, transmittance));
        const float3 ground = groundColor.rgb * groundLight.rgb;
        color = lerp(haze, ground, 1.0 - exp(direction.y / HorizonFade));
    }

    // Noise of under a step of an 8-bit display hides the banding of the sky's smooth gradients. Those steps are even
    // in sRGB, not in linear light, so the noise is added there.
    const float noise = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
    color = pow(max(pow(color, 1.0 / 2.2) + (noise - 0.5) / 255.0, 0.0), 2.2);
    return float4(color, 1.0);
}
