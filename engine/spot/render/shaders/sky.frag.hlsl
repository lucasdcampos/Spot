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
    // The clouds' coverage, density, height and size in meters; their offset along X and Z and their softness; and
    // their color.
    float4 clouds;
    float4 cloudMotion;
    float4 cloudColor;
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

struct Input {
    float4 position : SV_Position;
    float4 direction : TEXCOORD0;
};

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

// The light from the direction, as the sky lights the scene with it, and as GetSkyLighting() says on the CPU. There is
// no sun, which the scene's directional light already is, nor fog, which surfaces get on their own.
float3 GetCapturedLight(float3 direction, float3 toSun) {
    float3 transmittance;
    const float3 level = float3(direction.x, 0.0, direction.z);
    const float3 horizon = dot(level, level) > 1e-8 ? normalize(level) : float3(1.0, 0.0, 0.0);
    const float3 air = GetSkyLight(direction.y >= 0.0 ? direction : horizon, toSun, transmittance);
    float3 light = air;
    if (direction.y < 0.0) {
        light = lerp(air, groundColor.rgb * groundLight.rgb, 1.0 - exp(direction.y / HorizonFade));
    }
    return light * capture.y;
}

// Hashes from 0 to 1 made only of arithmetic, so every backend gives the same ones.
float Hash(float2 p) {
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float Hash(float3 p) {
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise(float2 p) {
    const float2 cell = floor(p);
    const float2 f = p - cell;
    const float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash(cell), Hash(cell + float2(1.0, 0.0)), u.x),
                lerp(Hash(cell + float2(0.0, 1.0)), Hash(cell + float2(1.0, 1.0)), u.x), u.y);
}

// Fractal noise, from 0 to 1 and about a half on average, with octaves turned from one another so their grids do not
// line up.
float CloudNoise(float2 p, int octaves) {
    float sum = 0.0;
    float amplitude = 0.5;
    float total = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (i >= octaves) {
            break;
        }
        sum += ValueNoise(p) * amplitude;
        total += amplitude;
        p = float2(0.8 * p.x - 0.6 * p.y, 0.6 * p.x + 0.8 * p.y) * 2.03 + 17.0;
        amplitude *= 0.5;
    }
    return sum / total;
}

// The light the clouds send from a direction above the horizon, in the sky's units, and how much of what is behind
// them they hide. The sky's own light there lights their shaded sides, as the sky lights the ground.
float4 GetClouds(float3 direction, float3 toSun, float3 sky) {
    const float coverage = clouds.x;
    if (coverage <= 0.0 || direction.y <= 0.0) {
        return 0.0;
    }
    // Where the ray meets the layer, in clouds.
    const float distance = clouds.z / max(direction.y, 1e-3);
    const float2 position = (direction.xz * distance + cloudMotion.xy) / clouds.w;
    // The noise is mostly between 0.2 and 0.8: full coverage takes all of it.
    const float threshold = lerp(0.82, 0.18, coverage);
    const float soft = 0.02 + cloudMotion.z * 0.3;
    const float shape = CloudNoise(position, 6);
    float opacity = smoothstep(threshold, threshold + soft, shape) * clouds.y;
    // Far away they fade into the haze, and where a pixel spans more than a cloud, into their average.
    const float footprint = length(fwidth(position));
    opacity *= exp(-distance / (clouds.z * 20.0)) * smoothstep(0.0, 0.06, direction.y) * saturate(1.5 - footprint * 2.0);
    if (opacity <= 0.0) {
        return 0.0;
    }
    // Thicker toward the sun, a cloud is darker; thin edges facing the sun glow with the light passing through.
    const float thickness = saturate((shape - threshold) / 0.25);
    const float2 towardSun = toSun.xz / max(toSun.y, 0.25) * 0.15;
    const float shade = saturate((CloudNoise(position + towardSun, 3) - threshold) / 0.25);
    const float lit = 0.3 + 0.7 * exp(-shade * 2.0);
    const float forward = pow(saturate(dot(direction, toSun)), 8.0) * (1.0 - thickness);
    const float3 sunlight = sunLight.rgb * lit * (1.3 + 2.0 * forward);
    // Moonlit clouds stand out gray against the night sky.
    const float3 moonlit = (nightColor.rgb * moon.y * 3.0 + moonlight.rgb * 0.6) * lit;
    const float3 light = cloudColor.rgb * (sunlight + moonlit + sky * (0.75 - 0.3 * thickness));
    return float4(light, opacity);
}

float3 Rotate(float3 v, float3 axis, float angle) {
    const float s = sin(angle);
    const float c = cos(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

// How bright a star of the brightest kind is, and how bright the moon and its glow are, before the exposure.
static const float StarBrightness = 0.5;
static const float MoonBrightness = 0.9;
static const float MoonGlowBrightness = 0.04;

// Stars a pixel or so wide, most of them faint, scattered over cells of the turning sky, one at most in each.
float3 GetStars(float3 direction) {
    if (stars.x <= 0.0 || stars.y <= 0.0) {
        return 0.0;
    }
    const float Cells = 220.0;
    const float3 p = Rotate(direction, starAxis.xyz, -stars.z) * Cells;
    const float3 cell = floor(p);
    if (Hash(cell) > stars.y * 0.06) {
        return 0.0;
    }
    const float3 jitter = float3(Hash(cell + 17.1), Hash(cell + 31.7), Hash(cell + 47.3));
    const float3 center = normalize(cell + 0.25 + jitter * 0.5);
    // Their distance to the star across the sky, in cells, against the width of a pixel there.
    const float offset = length(cross(center, p / Cells)) * Cells;
    const float pixel = max(length(fwidth(p)), 1e-4);
    // Seen from too far for a cell to cover a pixel, a star spreads its light over the pixel instead.
    const float spot = exp(-offset * offset / (pixel * pixel * 0.7)) * saturate(1.0 / (pixel * pixel));
    const float magnitude = 0.04 + pow(Hash(cell + 5.3), 7.0);
    const float twinkle = 0.75 + 0.25 * sin(stars.w * (1.3 + 3.0 * jitter.x) + jitter.y * 6.2831853);
    const float3 tint = lerp(float3(0.72, 0.82, 1.0), float3(1.0, 0.86, 0.7), jitter.z);
    return tint * (spot * magnitude * twinkle * stars.x * StarBrightness);
}

// A full moon toward the direction, opposite the sun, its seas darker, with a faint glow around it; disc says how much of the pixel it
// covers.
float3 GetMoon(float3 direction, float3 toMoon, out float disc) {
    disc = 0.0;
    const float size = moon.x;
    if (size <= 0.0 || moon.y <= 0.0) {
        return 0.0;
    }
    const float3 away = direction - toMoon;
    const float r = length(away) / size;
    const float glow = MoonGlowBrightness * (exp(-r * r / 12.0) + 0.3 / (1.0 + r * r / 30.0));
    float3 light = float3(0.82, 0.88, 1.0) * glow;
    if (r < 1.2) {
        const float3 up = abs(toMoon.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        const float3 right = normalize(cross(up, toMoon));
        const float3 top = cross(toMoon, right);
        const float2 local = float2(dot(away, right), dot(away, top)) / size;
        disc = 1.0 - smoothstep(1.0 - max(fwidth(r), 1e-3) * 1.5, 1.0, r);
        const float limb = sqrt(saturate(1.0 - r * r));
        const float seas = smoothstep(0.45, 0.65, CloudNoise(local * 1.6 + 3.0, 4));
        const float albedo = (0.95 - 0.35 * seas) * (0.65 + 0.35 * limb);
        light = lerp(light, float3(0.93, 0.95, 1.0) * albedo * MoonBrightness, disc);
    }
    return light * moon.y;
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

    // What is in front of the sun's core: the clouds' opacity.
    float cover = 0.0;
    if (direction.y >= 0.0) {
        const float3 sky = GetSkyLight(direction, toSun, transmittance);
        float3 light = sky + glow;
        // Stars and the moon come out as the sun sets, dimmed by the air in front of them.
        const float night = sunDirection.w > 0.5 ? saturate((0.05 - toSun.y) / 0.2) : 1.0;
        if (night > 0.0) {
            float disc = 0.0;
            float3 moonLight = 0.0;
            if (sunDirection.w > 0.5) {
                moonLight = GetMoon(direction, -toSun, disc);
            }
            light += (GetStars(direction) * (1.0 - disc) + moonLight) * night * transmittance;
        }
        const float4 cloud = GetClouds(direction, toSun, sky);
        light = lerp(light, cloud.rgb, cloud.a);
        cover = cloud.a;
        color = Expose(light);
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
        const float opacity = core * saturate(4.0 * brightest / max(source, 1e-4)) * (1.0 - cover);
        const float3 coreLight = sunLight.rgb / brightest * SunCoreBrightness;
        // HDR keeps the atmosphere and halo as radiance: replacing them by a fixed core could darken the sun's
        // center when its halo is brighter. Add the core's radiance before bloom and tonemapping instead.
        color = settings.w > 0.5 ? color + coreLight * settings.z * opacity
                                : lerp(color, 1.0 - exp(-coreLight), opacity);
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
