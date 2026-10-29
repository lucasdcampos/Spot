// The sky's uniforms and its atmosphere, which sky.frag.hlsl draws and the clouds' shaders light their clouds with.

cbuffer Sky : register(b0, space3) {
    // The direction toward the sun, and 1 in w when there is a sun.
    float4 sunDirection;
    // Premultiplied by the sun's intensity.
    float4 sunColor;
    float4 skyTint;
    float4 groundColor;
    // The light that reaches the ground, from the sun through the air and from the sky, like a surface of the scene.
    float4 groundLight;
    // The sunlight that crosses the air to the viewer, which fades out as the sun sets.
    float4 sunLight;
    // The moon's light before the air dims it, with 1 in w when it lights the air; the moon is opposite the sun.
    float4 moonlight;
    float4 nightColor;
    // The sun's angular radius in radians, the atmosphere's thickness, the exposure, and 1 in w when drawing into an
    // HDR target, which a tonemapper brings within the display's range later.
    float4 settings;
    // 1 in x when capturing the sky's light for the scene's lighting, with the scale of that light in y.
    float4 capture;
    // The stars' brightness, density, rotation in radians and time in seconds, the axis they turn around, and the
    // moon's angular radius and brightness.
    float4 stars;
    float4 starAxis;
    float4 moon;
};

#include "fog.hlsl"

// The atmosphere's tables (render/atmosphere.h): what crosses the air, and what it scatters more than once. A shader
// that reads other textures first defines the registers after them; one that needs only the uniforms defines
// SKY_WITHOUT_TABLES, which leaves out the functions that read them.
#ifndef SKY_WITHOUT_TABLES
#ifndef SKY_TABLES_TEXTURE0
#define SKY_TABLES_TEXTURE0 t0
#define SKY_TABLES_SAMPLER0 s0
#define SKY_TABLES_TEXTURE1 t1
#define SKY_TABLES_SAMPLER1 s1
#endif
Texture2D<float4> transmittanceTable : register(SKY_TABLES_TEXTURE0, space2);
SamplerState transmittanceSampler : register(SKY_TABLES_SAMPLER0, space2);
Texture2D<float4> scatteringTable : register(SKY_TABLES_TEXTURE1, space2);
SamplerState scatteringSampler : register(SKY_TABLES_SAMPLER1, space2);
#endif

// An Earth-like atmosphere integrated as Hillaire (2020) does, in kilometers: single scattering along the view, with
// the light scattered more times read from a table. Its coefficients are Bruneton's (2017) for a planet's air a fifth
// denser, under the haze of an ordinary clear day, five times his cleanest air's, a little wider in height and less
// forward: a sky that is bright by day, paler toward the horizon, with a wide glow around the sun, rather than the
// deep blue of air without haze. The viewer stands a little above a planet's surface. render/atmosphere.cpp computes the same on the CPU to light the scene and builds the tables, so
// the constants and the integration must match.
static const float PlanetRadius = 6360.0;
static const float AtmosphereRadius = 6420.0;
static const float ViewerHeight = 0.05;
// The heights over which the density of air molecules (Rayleigh) and of haze (Mie) falls by a factor of e.
static const float RayleighHeight = 8.0;
static const float MieHeight = 1.6;
// Per kilometer at sea level. Air scatters blue the most, which colors the sky; haze scatters all colors alike,
// mostly forward, which makes the glow around the sun; ozone, in a layer around 25 km up, scatters nothing but takes
// orange light, which keeps the sky overhead blue while the sun sets.
static const float3 RayleighScattering = float3(6.96e-3, 16.27e-3, 39.72e-3);
static const float MieScattering = 20.0e-3;
static const float MieExtinction = 22.2e-3;
static const float MieAnisotropy = 0.75;
static const float3 OzoneAbsorption = float3(0.650e-3, 1.881e-3, 0.085e-3);
static const float OzoneCenter = 25.0;
static const float OzoneWidth = 15.0;
static const float Pi = 3.14159265;

#define VIEW_SAMPLES 24
// Must match AtmosphereTables.
#define TRANSMITTANCE_WIDTH 256.0
#define TRANSMITTANCE_HEIGHT 64.0
#define SCATTERING_SIZE 32.0

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

// Texels hold the values at their centers, so the coordinates from 0 to 1 are moved half a texel in from each edge.
float ToTexel(float x, float size) {
    return 0.5 / size + x * (1.0 - 1.0 / size);
}

#ifndef SKY_WITHOUT_TABLES
// Of the light from a direction whose cosine with straight up is mu, what reaches the point at the radius r; none when
// the planet is in the way.
float3 GetTransmittance(float r, float mu) {
    if (GetGroundDistance(r, mu) > 0.0) {
        return 0.0;
    }
    const float top = sqrt(AtmosphereRadius * AtmosphereRadius - PlanetRadius * PlanetRadius);
    const float rho = sqrt(max(r * r - PlanetRadius * PlanetRadius, 0.0));
    const float nearest = AtmosphereRadius - r;
    const float farthest = rho + top;
    const float2 uv =
        float2(ToTexel((GetExitDistance(r, mu, AtmosphereRadius) - nearest) / max(farthest - nearest, 1e-6),
                       TRANSMITTANCE_WIDTH),
               ToTexel(rho / top, TRANSMITTANCE_HEIGHT));
    return transmittanceTable.SampleLevel(transmittanceSampler, uv, 0.0).rgb;
}

float3 GetMultipleScattering(float r, float muSun) {
    const float2 uv = float2(ToTexel(muSun * 0.5 + 0.5, SCATTERING_SIZE),
                             ToTexel((r - PlanetRadius) / (AtmosphereRadius - PlanetRadius), SCATTERING_SIZE));
    return scatteringTable.SampleLevel(scatteringSampler, uv, 0.0).rgb;
}
#endif

// What the air scatters by molecules and by haze, and takes, at the radius r.
void GetMedium(float r, out float3 rayleigh, out float mie, out float3 extinction) {
    const float thickness = settings.y;
    const float height = max(r - PlanetRadius, 0.0);
    const float rayleighDensity = exp(-height / RayleighHeight);
    const float mieDensity = exp(-height / MieHeight);
    const float ozoneDensity = max(1.0 - abs(height - OzoneCenter) / OzoneWidth, 0.0);
    rayleigh = RayleighScattering * skyTint.rgb * (rayleighDensity * thickness);
    mie = MieScattering * mieDensity * thickness;
    extinction =
        (RayleighScattering * rayleighDensity + MieExtinction * mieDensity + OzoneAbsorption * ozoneDensity) * thickness;
}

// How much of what is scattered along a step reaches its start, per unit of scattering: the integral of the
// transmittance over the step.
float3 GetStepIntegral(float3 extinction, float length) {
    const float3 safe = max(extinction, 1e-6);
    return (1.0 - exp(-safe * length)) / safe;
}

float GetRayleighPhase(float nu) {
    return 3.0 / (16.0 * Pi) * (1.0 + nu * nu);
}

// Cornette and Shanks's.
float GetMiePhase(float nu) {
    const float g2 = MieAnisotropy * MieAnisotropy;
    return 3.0 / (8.0 * Pi) * (1.0 - g2) * (1.0 + nu * nu) /
           ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * MieAnisotropy * nu, 1e-4), 1.5));
}

// Below this, as the sine of its height, a light leaves the whole sky in the planet's shadow, even what it scatters
// more than once.
static const float ShadowedLightHeight = -0.3;

#ifndef SKY_WITHOUT_TABLES
// The radiance the air scatters toward the viewer from a direction at or above the horizon, from the sun and from the
// moon opposite it, each for an irradiance of 1, and how much of what lies beyond the air gets through it. Both share
// the way through the air; a light it cannot reach is left out.
void GetSkyRadiance(float3 direction, float3 toSun, out float3 sunRadiance, out float3 moonRadiance,
                    out float3 transmittance) {
    const float r0 = PlanetRadius + ViewerHeight;
    const float mu = max(direction.y, 0.0);
    const float muSun = toSun.y;
    const float nu = dot(direction, toSun);
    const float distance = GetExitDistance(r0, mu, AtmosphereRadius);
    const bool sunLit = sunDirection.w > 0.5 && muSun > ShadowedLightHeight;
    const bool moonLit = moonlight.w > 0.5 && -muSun > ShadowedLightHeight;
    const float rayleighPhase = GetRayleighPhase(nu);
    const float sunMiePhase = GetMiePhase(nu);
    const float moonMiePhase = GetMiePhase(-nu);

    // Rays along the ground cross hundreds of kilometers of dense air, so the steps start short near the viewer.
    transmittance = 1.0;
    sunRadiance = 0.0;
    moonRadiance = 0.0;
    for (int i = 0; i < VIEW_SAMPLES; ++i) {
        const float start = distance * pow(float(i) / VIEW_SAMPLES, 2.0);
        const float end = distance * pow(float(i + 1) / VIEW_SAMPLES, 2.0);
        const float t = (start + end) * 0.5;
        const float r = sqrt(r0 * r0 + t * t + 2.0 * r0 * t * mu);
        const float pointMuSun = clamp((r0 * muSun + t * nu) / r, -1.0, 1.0);
        float3 rayleigh;
        float mie;
        float3 extinction;
        GetMedium(r, rayleigh, mie, extinction);
        const float3 reaching = transmittance * GetStepIntegral(extinction, end - start);
        if (sunLit) {
            const float3 sun = GetTransmittance(r, pointMuSun);
            const float3 multiple = GetMultipleScattering(r, pointMuSun);
            sunRadiance +=
                reaching * (rayleigh * (sun * rayleighPhase + multiple) + mie * (sun * sunMiePhase + multiple));
        }
        if (moonLit) {
            const float3 moon = GetTransmittance(r, -pointMuSun);
            const float3 multiple = GetMultipleScattering(r, -pointMuSun);
            moonRadiance +=
                reaching * (rayleigh * (moon * rayleighPhase + multiple) + mie * (moon * moonMiePhase + multiple));
        }
        transmittance *= exp(-extinction * (end - start));
    }
}

// The light of the sky toward the direction, as drawn before the exposure, in the units surfaces are shown in, where a
// white matte surface receiving an irradiance of 1 shows 1: radiance times pi. It is as bright as it lights the scene,
// so what is in the shade looks lit by the sky it sees.
float3 GetSkyLight(float3 direction, float3 toSun, out float3 transmittance) {
    float3 sunRadiance;
    float3 moonRadiance;
    GetSkyRadiance(direction, toSun, sunRadiance, moonRadiance, transmittance);
    return (sunRadiance * sunColor.rgb * sunDirection.w + moonRadiance * moonlight.rgb) * Pi + nightColor.rgb;
}
#endif

// Maps unbounded light to what the target can show by its brightness alone, keeping its hue: the glow of a sunset
// stays orange instead of washing out to white as light mapped color by color would.
float3 Expose(float3 light) {
    // Into an HDR target, light stays as it is, for the tonemapper to map with everything else.
    if (settings.w > 0.5) {
        return light * settings.z;
    }
    const float luminance = dot(light, float3(0.2126, 0.7152, 0.0722));
    if (luminance <= 0.0) {
        return 0.0;
    }
    const float3 color = light * ((1.0 - exp(-luminance * settings.z)) / luminance);
    // Saturated colors can come out above 1 in a channel, so they are scaled back, smoothly, since a hard limit would
    // draw a visible edge where it starts.
    const float brightest = max(max(color.r, color.g), color.b);
    return color / pow(1.0 + pow(brightest, 8.0), 1.0 / 8.0);
}

// Noise of under a step of an 8-bit display hides the banding of the sky's smooth gradients. Those steps are even in
// sRGB, not in linear light, so the noise is added there. The tonemapper dithers HDR targets instead.
float3 Dither(float3 color, float2 pixel) {
    if (settings.w > 0.5) {
        return color;
    }
    const float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    return pow(max(pow(color, 1.0 / 2.2) + (noise - 0.5) / 255.0, 0.0), 2.2);
}
