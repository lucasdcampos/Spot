cbuffer Sky : register(b0, space3) {
    // The direction toward the sun, and 1 in w when there is a sun.
    float4 sunDirection;
    // Premultiplied by the sun's intensity.
    float4 sunColor;
    float4 skyTint;
    float4 groundColor;
    float4 nightColor;
    // The cosine of the sun's angular radius, the atmosphere's thickness and the exposure.
    float4 settings;
};

struct Input {
    float4 position : SV_Position;
    float4 direction : TEXCOORD0;
};

// Single scattering in an Earth-like atmosphere (Nishita et al. 1993), in kilometers. The viewer stands a little above
// a planet's surface, so rays that hit it show the ground, hazed by the air in between near the horizon.
static const float PlanetRadius = 6360.0;
static const float AtmosphereRadius = 6420.0;
static const float ViewerHeight = 0.05;
// The heights over which the density of air molecules (Rayleigh) and of haze (Mie) falls by a factor of e.
static const float RayleighHeight = 8.0;
static const float MieHeight = 1.2;
// Per kilometer at sea level. Air scatters blue the most, which colors the sky; haze scatters all colors alike,
// mostly forward, which makes the glow around the sun.
static const float3 RayleighScattering = float3(5.8e-3, 13.5e-3, 33.1e-3);
static const float MieScattering = 21e-3;
static const float MieExtinction = MieScattering * 1.11;
static const float MieAnisotropy = 0.76;
// Sunlight is far brighter than what the sky scatters of it, and the disc brighter still.
static const float SunIrradiance = 40.0;
static const float SunDiscRadiance = 40.0;
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

float3 GetTransmittance(float2 depth, float3 rayleigh, float mie) {
    return exp(-(rayleigh * depth.x + mie * MieExtinction / MieScattering * depth.y));
}

float4 main(Input input) : SV_Target0 {
    const float3 direction = normalize(input.direction.xyz / input.direction.w);
    const float3 toSun = sunDirection.xyz;
    const float thickness = settings.y;
    const float3 rayleigh = RayleighScattering * thickness;
    const float mie = MieScattering * thickness;

    const float r0 = PlanetRadius + ViewerHeight;
    const float mu = direction.y;
    const float muSun = toSun.y;
    const float nu = dot(direction, toSun);

    const float groundDistance = GetGroundDistance(r0, mu);
    const bool hitsGround = groundDistance > 0.0;
    const float distance = hitsGround ? groundDistance : GetExitDistance(r0, mu, AtmosphereRadius);

    // Sunlight scattered toward the viewer at each point along the ray, dimmed on its way from the sun and then to
    // the viewer. Rays along the ground cross hundreds of kilometers of dense air, so the steps start short near the
    // viewer, and each point is dimmed only by the air up to it, not by its whole step.
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
            const float3 transmittance = GetTransmittance(viewDepth + density * 0.5 + sunDepth, rayleigh, mie);
            rayleighSum += density.x * transmittance;
            mieSum += density.y * transmittance;
        }
        viewDepth += density;
    }

    const float rayleighPhase = 3.0 / (16.0 * Pi) * (1.0 + nu * nu);
    const float g2 = MieAnisotropy * MieAnisotropy;
    const float miePhase = 3.0 / (8.0 * Pi) * (1.0 - g2) * (1.0 + nu * nu) /
                           ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * MieAnisotropy * nu, 1e-4), 1.5));
    const float3 sunlight = sunColor.rgb * SunIrradiance * sunDirection.w;
    float3 color = sunlight * (rayleighSum * rayleigh * rayleighPhase * skyTint.rgb + mieSum * mie * miePhase) +
                   nightColor.rgb;

    const float3 viewTransmittance = GetTransmittance(viewDepth, rayleigh, mie);
    if (hitsGround) {
        // The ground reflects the sunlight that reaches it evenly in all directions.
        float2 sunDepth;
        if (GetSunOpticalDepth(PlanetRadius, muSun, sunDepth)) {
            const float3 irradiance = sunlight * GetTransmittance(sunDepth, rayleigh, mie) * max(muSun, 0.0);
            color += viewTransmittance * groundColor.rgb * irradiance / Pi;
        }
        // The ground reflects the night sky's light too, instead of the sky's own glow.
        color += (groundColor.rgb - 1.0) * nightColor.rgb;
    } else if (sunDirection.w > 0.5 && settings.x < 1.0) {
        const float cosRadius = settings.x;
        const float disc = smoothstep(cosRadius, cosRadius + (1.0 - cosRadius) * 0.3, nu);
        color += viewTransmittance * sunlight * SunDiscRadiance * disc;
    }

    // Maps the unbounded light to what the target can show, keeping bright skies from clipping to white, then encodes
    // it for display, as the colors of the rest of the scene already are.
    color = pow(1.0 - exp(-color * settings.z), 1.0 / 2.2);
    // Noise of under a step of the target's color hides the banding of the sky's smooth gradients.
    const float noise = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
    color += (noise - 0.5) / 255.0;
    return float4(color, 1.0);
}
