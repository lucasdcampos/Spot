#include "sky.hlsl"

struct Input {
    float4 position : SV_Position;
    float4 direction : TEXCOORD0;
};

// The sun is far brighter than anything else in view, so, as in a camera or the eye, its light spills over what is
// around it: a blinding core, an intense glow a few radii wide and a faint halo far beyond. All of it is light
// that crossed the air, so it turns into an orange ball of fire at sunset. The core shows the hue of that light at
// full brightness, white at noon and orange at sunset, since its brightness alone would burn out to white either way.
static const float SunCoreBrightness = 8.0;
static const float SunGlowBrightness = 10.0;
static const float SunHaloBrightness = 0.5;
// How far below the horizon, as the cosine of the angle from level, the ground fades in from the haze.
static const float HorizonFade = 0.08;

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
float FractalNoise(float2 p, int octaves) {
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
        const float seas = smoothstep(0.45, 0.65, FractalNoise(local * 1.6 + 3.0, 4));
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

    if (direction.y >= 0.0) {
        float3 light = GetSkyLight(direction, toSun, transmittance) + glow;
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
        const float opacity = core * saturate(4.0 * brightest / max(source, 1e-4));
        const float3 coreLight = sunLight.rgb / brightest * SunCoreBrightness;
        // HDR keeps the atmosphere and halo as radiance: replacing them by a fixed core could darken the sun's
        // center when its halo is brighter. Add the core's radiance before bloom and tonemapping instead.
        color = settings.w > 0.5 ? color + coreLight * settings.z * opacity
                                : lerp(color, 1.0 - exp(-coreLight), opacity);
    }
    // Fog toward the horizon, as for a surface infinitely far away.
    color = lerp(color, fogColor.rgb, GetFogAmount(direction * 1e5));
    return float4(Dither(color, input.position.xy), 1.0);
}
