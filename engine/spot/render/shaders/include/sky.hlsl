// The sky's uniforms and its atmosphere, which sky.frag.hlsl draws and clouds.frag.hlsl lights its clouds with.

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

// The fog's color, with 1 in w when there is fog; its density, start, height falloff and base height; and the camera's
// position.
cbuffer Fog : register(b1, space3) {
    float4 fogColor;
    float4 fogParams;
    float4 fogCamera;
};

// How much of the light from a point the fog takes on its way to the viewer: exponential height fog, integrated in
// closed form along the ray from where the fog starts.
float GetFogAmount(float3 toPoint) {
    if (fogColor.w < 0.5) {
        return 0.0;
    }
    const float distance = length(toPoint);
    const float fogged = max(distance - fogParams.y, 0.0);
    if (fogged <= 0.0) {
        return 0.0;
    }
    const float3 direction = toPoint / max(distance, 1e-5);
    const float startHeight = fogCamera.y + direction.y * (distance - fogged) - fogParams.w;
    const float falloff = fogParams.z;
    float depth = fogParams.x * fogged;
    if (falloff > 1e-5) {
        // The fog thins along the ray as it climbs; flat rays see it as it is. The density at each end is taken on
        // its own, since a ray going far down from high above the fog would multiply a density that rounds to 0 by a
        // growth that overflows.
        const float endHeight = startHeight + direction.y * fogged;
        const float rise = falloff * (endHeight - startHeight);
        const float startDensity = exp(-falloff * startHeight);
        depth *= abs(rise) > 1e-4 ? (startDensity - exp(-falloff * endHeight)) / rise : startDensity;
    }
    return 1.0 - exp(-depth);
}

// Single scattering in an Earth-like atmosphere (Nishita et al. 1993, with Bruneton's 2017 coefficients), in
// kilometers. The viewer stands a little above a planet's surface. render/atmosphere.cpp computes the same on the
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
// How bright the sky is for a sun of intensity 1, before the exposure, in the units surfaces are shown in, where a white
// matte surface receiving an irradiance of 1 shows 1. It is drawn as bright as it lights the scene, so what is in the
// shade looks lit by the sky it sees. Radiance takes pi to those units; the rest makes up for the light the air
// scatters more than once and the haze of real air, which single scattering leaves out. Must match SkyBrightness in
// renderer_3d_sky.cpp.
static const float SkyBrightness = 20.0;
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

// Below this, as the sine of its height, a light leaves even the top of the air in the planet's shadow.
static const float ShadowedLightHeight = -0.15;

// The light the air scatters toward the viewer from a direction at or above the horizon, from the sun and from the
// moon opposite it, each for an irradiance of 1, and how much of what lies beyond the air gets through it. Both share
// the way through the air; a light it cannot reach is left out.
void GetSkyRadiance(float3 direction, float3 toSun, out float3 sunRadiance, out float3 moonRadiance,
                    out float3 transmittance) {
    const float r0 = PlanetRadius + ViewerHeight;
    const float mu = max(direction.y, 0.0);
    const float muSun = toSun.y;
    const float nu = dot(direction, toSun);
    const float thickness = settings.y;
    const float distance = GetExitDistance(r0, mu, AtmosphereRadius);
    const bool sunLit = sunDirection.w > 0.5 && muSun > ShadowedLightHeight;
    const bool moonLit = moonlight.w > 0.5 && -muSun > ShadowedLightHeight;

    // Rays along the ground cross hundreds of kilometers of dense air, so the steps start short near the viewer, and
    // each point is dimmed only by the air up to it, not by its whole step.
    float2 viewDepth = 0.0;
    float3 sunRayleigh = 0.0;
    float3 sunMie = 0.0;
    float3 moonRayleigh = 0.0;
    float3 moonMie = 0.0;
    for (int i = 0; i < VIEW_SAMPLES; ++i) {
        const float start = distance * pow(float(i) / VIEW_SAMPLES, 2.0);
        const float end = distance * pow(float(i + 1) / VIEW_SAMPLES, 2.0);
        const float t = (start + end) * 0.5;
        const float r = sqrt(r0 * r0 + t * t + 2.0 * r0 * t * mu);
        const float2 density = GetDensity(r) * (end - start);
        const float2 depth = viewDepth + density * 0.5;
        float2 lightDepth;
        if (sunLit && GetSunOpticalDepth(r, (r0 * muSun + t * nu) / r, lightDepth)) {
            const float3 pathTransmittance = GetTransmittance(depth + lightDepth);
            sunRayleigh += density.x * pathTransmittance;
            sunMie += density.y * pathTransmittance;
        }
        if (moonLit && GetSunOpticalDepth(r, -(r0 * muSun + t * nu) / r, lightDepth)) {
            const float3 pathTransmittance = GetTransmittance(depth + lightDepth);
            moonRayleigh += density.x * pathTransmittance;
            moonMie += density.y * pathTransmittance;
        }
        viewDepth += density;
    }
    transmittance = GetTransmittance(viewDepth);

    const float rayleighPhase = 3.0 / (16.0 * Pi) * (1.0 + nu * nu);
    const float g2 = MieAnisotropy * MieAnisotropy;
    const float mieScale = 3.0 / (8.0 * Pi) * (1.0 - g2) * (1.0 + nu * nu) / (2.0 + g2);
    const float sunMiePhase = mieScale / pow(max(1.0 + g2 - 2.0 * MieAnisotropy * nu, 1e-4), 1.5);
    const float moonMiePhase = mieScale / pow(max(1.0 + g2 + 2.0 * MieAnisotropy * nu, 1e-4), 1.5);
    const float3 rayleigh = RayleighScattering * rayleighPhase * skyTint.rgb;
    sunRadiance = (sunRayleigh * rayleigh + sunMie * MieScattering * sunMiePhase) * thickness;
    moonRadiance = (moonRayleigh * rayleigh + moonMie * MieScattering * moonMiePhase) * thickness;
}

// The light of the sky toward the direction, as drawn before the exposure.
float3 GetSkyLight(float3 direction, float3 toSun, out float3 transmittance) {
    float3 sunRadiance;
    float3 moonRadiance;
    GetSkyRadiance(direction, toSun, sunRadiance, moonRadiance, transmittance);
    return (sunRadiance * sunColor.rgb * sunDirection.w + moonRadiance * moonlight.rgb) * SkyBrightness +
           nightColor.rgb;
}

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
