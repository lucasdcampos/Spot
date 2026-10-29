#include "sky.hlsl"
#include "clouds.hlsl"

// The sky's layer of clouds, drawn over the view after its opaque surfaces. Each pixel's ray meets the layer, a sphere
// around the planet at the clouds' altitude, wherever the camera is: from below, or from above when flying over them.
// The noise there shapes the clouds and the light lights them as the sky is lit. The shader gives the depth where the
// ray meets them, so what is nearer hides them, and they hide what is beyond, such as the ground seen from above.

// render/cloud_noise.h tells what each channel holds.
Texture2D<float4> cloudNoise : register(t0, space2);
SamplerState cloudSampler : register(s0, space2);

float4 SampleNoise(float2 uv, float2 dx, float2 dy) {
    return cloudNoise.SampleGrad(cloudSampler, uv, dx, dy);
}

// The clouds' shape at a point of their noise, from its formations and its single clouds.
float GetShape(float2 uv, float2 dx, float2 dy) {
    return CombineShapes(SampleNoise(ToFormationUv(uv), ToFormationUv(dx), ToFormationUv(dy)).r,
                         SampleNoise(uv, dx, dy).r);
}

struct Input {
    float4 position : SV_Position;
    float4 direction : TEXCOORD0;
};

struct Output {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};

// The optical depth of a cloud where it is densest: all but a trace of the light going straight through it is
// scattered, as in real clouds, which still let a share of it through, scattered.
static const float CloudOpticalDepth = 20.0;
// How tall a cloud stands, as a share of its width, where it is densest: those around one toward a low light shade it.
static const float CloudHeight = 0.5;
// How far toward the light, in clouds, they are looked for, and how much a cloud standing a cloud's width above the
// light's way to another hides it, in powers of e.
static const float ShadowReach = 1.0;
static const float ShadowStrength = 4.0;
#define LIGHT_SAMPLES 4
// Thin parts of clouds in front of the light glow with what they scatter forward, like the lining of a cloud in front
// of the sun.
static const float ForwardAnisotropy = 0.6;
static const float ForwardBrightness = 0.25;
// Within this many meters of the layer's altitude, a camera crossing it sees the layer fade out, as there is no inside
// to fly through.
static const float CrossingDistance = 30.0;

// The distance along the ray to where it meets the layer, or a negative number when it does not, or the ground comes
// first. The layer and the ground are spheres around the planet's center, under the camera, and the heights are
// above the ground: differences of heights stay exact where differences of radii would round to half a meter.
float GetLayerDistance(float height, float altitude, float mu) {
    const float b = (PlanetRadiusMeters + height) * mu;
    const float c = (height - altitude) * (2.0 * PlanetRadiusMeters + height + altitude);
    const float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return -1.0;
    }
    const float root = sqrt(discriminant);
    if (c < 0.0) {
        // From below, rays leave the layer's sphere, unless they go down into the ground first.
        const float ground = height * (2.0 * PlanetRadiusMeters + height);
        if (mu < 0.0 && b * b >= ground) {
            return -1.0;
        }
        return mu >= 0.0 ? -c / (b + root) : -b + root;
    }
    // From above, only rays going down meet it, at the nearer of the two points.
    return mu < 0.0 ? c / (-b + root) : -1.0;
}

Output main(Input input) {
    Output output;
    const float3 direction = normalize(input.direction.xyz / input.direction.w);
    const float3 camera = view.xyz;
    const float altitude = view.w;
    const float distance = GetLayerDistance(camera.y, altitude, direction.y);

    // Where the ray meets the layer, in clouds and in the noise's texture, whose derivatives are taken before any
    // pixel leaves, for every sample to filter by.
    const float2 position = (camera.xz + direction.xz * max(distance, 0.0) - cloudMotion.xy) / cloudShape.w;
    const float2 uv = position / cloudMotion.z;
    const float2 dx = ddx(uv);
    const float2 dy = ddy(uv);
    const float coverage = cloudShape.x;
    if (distance <= 0.0 || coverage <= 0.0) {
        discard;
    }

    const float softness = cloudShape.z;
    const float local = GetLocalCoverage(coverage, SampleNoise(ToWeatherUv(uv), ToWeatherUv(dx), ToWeatherUv(dy)).a);
    const float threshold = GetThreshold(local);
    const float shape = GetShape(uv, dx, dy);
    const float4 detail = SampleNoise(uv * DetailScale, dx * DetailScale, dy * DetailScale);
    const float density = GetDensity(shape, detail, threshold, local, softness);
    // Where a pixel spans much of a cloud, it shows their average instead, which does not shimmer.
    const float footprint = max(length(dx), length(dy)) * cloudMotion.z;
    const float far = smoothstep(0.1, 0.5, footprint);
    // Each billow is a puff thicker in its middle, which shows, from below, as darker lumps across a cloud.
    const float sharpDepth = CloudOpticalDepth * pow(density, lerp(0.5, 1.5, softness)) * (0.4 + 1.2 * detail.g);
    const float averageDepth = CloudOpticalDepth * 0.35;
    // Seen at a slant, the view crosses more of the layer: it sees less through it, and more of the light it scatters.
    const float layerCos = abs((PlanetRadiusMeters + camera.y) * direction.y + distance) /
                           (PlanetRadiusMeters + altitude);
    const float slant = 1.0 / max(layerCos, 0.2);
    const float opticalDepth = lerp(sharpDepth, averageDepth, far) * slant;
    const float opacity = lerp(1.0 - exp(-sharpDepth * slant), local * (1.0 - exp(-averageDepth * slant)), far);
    if (opacity <= 0.0) {
        discard;
    }

    float3 toLight;
    const float3 light = GetCloudLight(toLight);

    // The clouds stand as tall as they are dense, so those toward a low light shade the sides of the others facing
    // away from it, while a high light reaches all their tops.
    const float across = length(toLight.xz);
    float occlusion = 0.0;
    if (across > 1e-3) {
        const float rise = toLight.y / across;
        const float2 stride = toLight.xz / across * (ShadowReach / LIGHT_SAMPLES);
        const float top = density * CloudHeight;
        for (int i = 1; i <= LIGHT_SAMPLES; ++i) {
            const float2 toward = (position + stride * i) / cloudMotion.z;
            const float neighbor = GetInside(GetShape(toward, dx, dy), threshold) * CloudHeight;
            occlusion += max(neighbor - top - rise * ShadowReach * i / LIGHT_SAMPLES, 0.0);
        }
    }
    const float visibility = lerp(exp(-occlusion * ShadowStrength), 1.0, far);

    // What a cloud sends toward the viewer for each part of the light it takes from behind, on the side the light
    // falls on and on the other: thick clouds reflect most of it and let a share through, and thin ones let most
    // through. Seen from below, the sun and the sky above light them through, and the ground lights their bases.
    const float extinction = max(1.0 - exp(-opticalDepth), 1e-4);
    const float transmitted = max(GetDiffuseTransmittance(opticalDepth) - exp(-opticalDepth), 0.0) / extinction;
    const float reflected = (1.0 - GetDiffuseTransmittance(opticalDepth)) / extinction;
    const float above = camera.y > altitude ? 1.0 : 0.0;
    const float facing = lerp(transmitted, reflected, above);
    const float backing = lerp(reflected, transmitted, above);
    // A low light still lights the sides of clouds, which are not flat.
    const float3 sunlit = light * lerp(0.5, 1.0, saturate(toLight.y)) * visibility;
    const float forward = max(GetPhase(dot(direction, toLight), ForwardAnisotropy) - 1.0, 0.0) * ForwardBrightness *
                          exp(-opticalDepth * 0.5) * (1.0 - above);
    const float3 ground = groundColor.rgb * groundLight.rgb;
    const float3 cloudLight =
        cloudColor.rgb * (sunlit * (facing + forward) + skylight.rgb * facing + ground * backing);

    float3 color = Expose(cloudLight);
    color = lerp(color, fogColor.rgb, GetFogAmount(direction * distance));
    // Far away they fade into the haze, which the sky behind them shows.
    const float haze = exp(-distance * max(settings.y, 0.05) / HazeDistance);
    const float crossing = saturate(abs(camera.y - altitude) / CrossingDistance);
    const float alpha = opacity * cloudShape.y * haze * crossing;
    output.color = float4(Dither(color, input.position.xy) * alpha, alpha);
    const float4 clip = mul(viewProjection, float4(direction * distance, 1.0));
    output.depth = saturate(clip.z / clip.w);
    return output;
}
