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
    float4 nightColor;
    // The sun's angular radius in radians, the atmosphere's thickness, the exposure, and 1 in w when drawing into an
    // HDR target, which a tonemapper brings within the display's range later.
    float4 settings;
    // 1 in x when capturing the sky's light for the scene's lighting, with the scale of that light in y.
    float4 capture;
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
        const float rise = falloff * direction.y * fogged;
        // The fog thins along the ray as it climbs; flat rays see it as it is.
        const float thinning = abs(rise) > 1e-4 ? (1.0 - exp(-rise)) / rise : 1.0;
        depth *= exp(-falloff * startHeight) * thinning;
    }
    return 1.0 - exp(-depth);
}

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
// The sun is far brighter than anything else in view, so, as in a camera or the eye, its light spills over what is
// around it: a blinding core, an intense glow a few radii wide and a faint halo far beyond. All of it is light
// that crossed the air, so it turns into an orange ball of fire at sunset. The core shows the hue of that light at
// full brightness, white at noon and orange at sunset, since its brightness alone would burn out to white either way.
static const float SunCoreBrightness = 8.0;
static const float SunGlowBrightness = 10.0;
static const float SunHaloBrightness = 0.5;
// How far below the horizon, as the cosine of the angle from level, the ground fades in from the haze.
static const float HorizonFade = 0.08;
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

// The atmosphere is single scattering, which leaves out the light the air scatters more than once, about as much again
// near the ground; must match MultipleScattering in renderer_3d.cpp.
static const float MultipleScattering = 2.0;

// The light from the direction in the units surfaces are shown in, where a white matte surface receiving an
// irradiance of 1 shows 1, so that the sky lights the scene as GetSkyLighting() says on the CPU: radiance times pi.
// There is no sun, which the scene's directional light already is, nor fog, which surfaces get on their own.
float3 GetCapturedLight(float3 direction, float3 toSun) {
    float3 transmittance;
    const float3 level = float3(direction.x, 0.0, direction.z);
    const float3 horizon = dot(level, level) > 1e-8 ? normalize(level) : float3(1.0, 0.0, 0.0);
    const float3 sunlight = sunColor.rgb * sunDirection.w;
    const float3 air = Pi * MultipleScattering * sunlight *
                           GetSkyRadiance(direction.y >= 0.0 ? direction : horizon, toSun, transmittance) +
                       nightColor.rgb;
    float3 light = air;
    if (direction.y < 0.0) {
        light = lerp(air, groundColor.rgb * groundLight.rgb, 1.0 - exp(direction.y / HorizonFade));
    }
    return light * capture.y;
}

float4 main(Input input) : SV_Target0 {
    const float3 direction = normalize(input.direction.xyz / input.direction.w);
    const float3 toSun = sunDirection.xyz;
    if (capture.x > 0.5) {
        return float4(GetCapturedLight(direction, toSun), 1.0);
    }
    float3 transmittance;
    float3 color;
    const float sunRadius = settings.x;
    const bool hasSun = sunDirection.w > 0.5 && sunRadius > 0.0;

    // The sun's glow, from light that crossed the air toward it, in sun radii from its center. It spills below the
    // horizon too, over the haze there.
    const float sunDistance = length(direction - toSun) / max(sunRadius, 1e-6);
    float3 glow = 0.0;
    if (hasSun) {
        glow = sunLight.rgb * (SunGlowBrightness * exp(-sunDistance * sunDistance / 9.0) +
                               SunHaloBrightness / (1.0 + sunDistance * sunDistance / 16.0));
    }

    if (direction.y >= 0.0) {
        color = Expose(GetSkyLight(direction, toSun, transmittance) + glow);
    } else {
        // The ground fades in below the horizon out of the haze there, so the two meet without a line.
        const float3 level = float3(direction.x, 0.0, direction.z);
        const float3 horizon = dot(level, level) > 1e-8 ? normalize(level) : float3(1.0, 0.0, 0.0);
        const float3 haze = Expose(GetSkyLight(horizon, toSun, transmittance) + glow);
        const float3 ground = groundColor.rgb * groundLight.rgb;
        color = lerp(haze, ground, 1.0 - exp(direction.y / HorizonFade));
    }

    // The core burns out to white wherever the sunlight has all its colors, so it shows the hue of that light at full
    // brightness, and fades out with the sunlight as the sun sets, compared to the sun's own brightness.
    const float brightest = max(max(sunLight.r, sunLight.g), sunLight.b);
    if (hasSun && direction.y >= 0.0 && brightest > 0.0) {
        // Fading from the center, so it merges into the glow instead of showing as a disc of its own.
        const float core = 1.0 - smoothstep(0.3, 1.0, sunDistance);
        const float source = max(max(sunColor.r, sunColor.g), sunColor.b);
        const float opacity = core * saturate(4.0 * brightest / max(source, 1e-4));
        const float3 coreLight = sunLight.rgb / brightest * SunCoreBrightness;
        color = lerp(color, settings.w > 0.5 ? coreLight * settings.z : 1.0 - exp(-coreLight), opacity);
    }
    // Fog toward the horizon, as for a surface infinitely far away.
    color = lerp(color, fogColor.rgb, GetFogAmount(direction * 1e5));

    // The tonemapper dithers HDR targets.
    if (settings.w > 0.5) {
        return float4(color, 1.0);
    }

    // Noise of under a step of an 8-bit display hides the banding of the sky's smooth gradients. Those steps are even
    // in sRGB, not in linear light, so the noise is added there.
    const float noise = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
    color = pow(max(pow(color, 1.0 / 2.2) + (noise - 0.5) / 255.0, 0.0), 2.2);
    return float4(color, 1.0);
}
