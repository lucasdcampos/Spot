#include "sky.hlsl"
#include "clouds.hlsl"
#define CLOUD_FRAME_REGISTER b3
#include "clouds_volume.hlsl"

// The sky's clouds as a volume, marched at half the view's size: each ray crosses the layer, from its base up to its
// top, until the scene's surfaces, and adds up the light the clouds along it scatter toward the camera and how much of
// what is behind them they hide. Where they are, the layer's shapes say how tall they rise, and noise in three
// dimensions carves them into billows. Each frame starts the rays a little further along, which the frames before
// even out (clouds_resolve.frag.hlsl).

Texture2D<float> sceneDepth : register(t0, space2);
SamplerState sceneDepthSampler : register(s0, space2);
Texture3D<float4> volumeNoise : register(t1, space2);
SamplerState volumeSampler : register(s1, space2);
// render/cloud_noise.h tells what each channel of both holds.
Texture2D<float4> cloudNoise : register(t2, space2);
SamplerState cloudSampler : register(s2, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

#define STEPS 56
#define LIGHT_STEPS 5
// How far a ray is marched through the clouds at most, and how far away they may start, in meters.
static const float MaxMarch = 20000.0;
static const float MaxStart = 90000.0;
// How much light a meter of cloud at its densest takes, at a density of 1: about what a cumulus does.
static const float Extinction = 0.06;
// The clouds' density is built in three levels, each only within what the one above made:
//   macrostructure  where clouds are, how large, how tall and how grouped: the weather, the layer's shapes and the
//                   volume noise's swells, the smooth blue channel, a third of a wave across a cloud
//   mesostructure   bulges, cavities and uneven density inside the formations: its billows, the red channel
//   microstructure  erosion, turbulence and fine detail at the edges: its fine billows, the green channel, swirled by
//                   the billows
// Each finer level fades out where a pixel spans more than one of its waves, as the volume noise has no mipmaps. It
// then takes what it would on average, which is known since the noise is equalized, so clouds keep the same mass and
// silhouette near and far. Scales are in waves across a cloud; depths say how far into a cloud each level carves.
static const float MacroSwellScale = 0.3;
static const float MacroSwellDepth = 0.4;
static const float MesoScale = 1.1;
static const float MesoBaseDepth = 0.2;
static const float MesoTopDepth = 0.7;
static const float MesoThinning = 0.65;
static const float MicroScale = 4.3;
static const float MicroDepth = 0.4;
static const float MicroThinning = 0.4;
static const float MicroSwirl = 0.35;
// The share of the layer's height that rays toward the light cross to find what shades a point, and how much more
// than the view's the light's way through a cloud dims it: the thick middle of a large cloud lies in its own shade.
static const float LightReach = 0.8;
static const float LightExtinction = 1.6;
// The most a cloud scatters toward the camera, relative to scattering evenly.
static const float MaxPhase = 3.0;
// The share of the sunlight a cloud field scatters about inside itself and from cloud to cloud, which reaches even
// the sides and bases the sun does not, as part of the light from all around.
static const float BouncedSunlight = 0.08;

// What is left of a density once noise takes the amount from it, spread back over 0 to 1.
float Erode(float density, float amount) {
    return saturate((density - amount) / max(1.0 - amount, 1e-3));
}

// How much of a level's effect shows where a pixel spans the clouds of the footprint, for noise of so many waves
// across a cloud: all of it while a wave spans a few pixels, none once a pixel spans a wave.
float GetLevelDetail(float footprint, float waves) {
    return saturate(1.5 - footprint * waves * 1.5);
}

// The macrostructure: where clouds are, how large and how tall. The weather says how much of the sky clouds cover and
// how tall they grow; the layer's shapes, read as large formations and single clouds, where they are, and the deeper
// inside one, the higher a cloud rises from its flat base; the swells raise some parts into towers and hollow others
// out. Also gives how far up its own height the point is.
float GetMacroDensity(float2 position, float altitude, float lod, out float upper) {
    upper = 0.0;
    const float height = (altitude - layer.y) / (layer.z - layer.y);
    if (height <= 0.0 || height >= 1.0) {
        return 0.0;
    }
    const float2 uv = position / cloudMotion.z;
    const float local =
        GetLocalCoverage(cloudShape.x, cloudNoise.SampleLevel(cloudSampler, ToWeatherUv(uv), 0.0).a);
    if (local <= 0.0) {
        return 0.0;
    }
    const float shape = CombineShapes(cloudNoise.SampleLevel(cloudSampler, ToFormationUv(uv), lod).r,
                                      cloudNoise.SampleLevel(cloudSampler, uv, lod).r);
    const float column = GetInside(shape, GetThreshold(local));
    if (column <= 0.0) {
        return 0.0;
    }
    const float3 at = float3(position.x, altitude / cloudShape.w, position.y);
    // The swells are a third of a wave across a cloud, so a pixel never spans one: they are always there.
    const float swell = volumeNoise.SampleLevel(volumeSampler, at * MacroSwellScale, 0.0).b;
    const float top = (0.2 + 0.8 * sqrt(column)) * lerp(0.4, 1.0, GetCloudGrowth(local)) * lerp(0.6, 1.2, swell);
    upper = saturate(height / max(top, 1e-3));
    const float density = column * smoothstep(0.0, 0.015, height) * (1.0 - smoothstep(top * 0.3, top, height));
    return Erode(density, (1.0 - swell) * MacroSwellDepth);
}

// The mesostructure: the billows raise bulges and dig cavities, little at the base and more toward the top, and thin
// the inside unevenly. Gives the billows read, which swirl the microstructure.
float ApplyMesostructure(float density, float3 at, float upper, float footprint, out float4 billows) {
    const float detail = GetLevelDetail(footprint, MesoScale * 4.0);
    billows = volumeNoise.SampleLevel(volumeSampler, at * MesoScale, 0.0);
    // Equalized, the billows are a half on average.
    const float billow = lerp(0.5, billows.r, detail);
    density = Erode(density, (1.0 - billow) * lerp(MesoBaseDepth, MesoTopDepth, upper));
    return density * lerp(1.0 - MesoThinning, 1.0, billow);
}

// The microstructure: fine billows, swirled by the billows as turbulence would, fray the edges into wisps and leave
// thinner pockets.
float ApplyMicrostructure(float density, float3 at, float4 billows, float footprint) {
    const float detail = GetLevelDetail(footprint, MicroScale * 8.0);
    const float3 swirled = at * MicroScale + (billows.rgb - 0.5) * MicroSwirl;
    const float fray = lerp(0.5, volumeNoise.SampleLevel(volumeSampler, swirled, 0.0).g, detail);
    density = Erode(density, (1.0 - fray) * MicroDepth);
    return density * lerp(1.0 - MicroThinning, 1.0, fray);
}

// How dense the cloud is at a point, from 0 to 1, from its macrostructure down; footprint is the clouds a pixel spans
// there. What only shades leaves out the microstructure, which would not change its shade.
float GetCloudDensity(float2 position, float altitude, float lod, float footprint, bool fine) {
    float upper;
    float density = GetMacroDensity(position, altitude, lod, upper);
    if (density <= 0.0) {
        return 0.0;
    }
    const float3 at = float3(position.x, altitude / cloudShape.w, position.y);
    float4 billows;
    density = ApplyMesostructure(density, at, upper, footprint, billows);
    if (fine && density > 0.0) {
        density = ApplyMicrostructure(density, at, billows, footprint);
    }
    return density;
}

// What a cloud scatters toward the camera, relative to scattering evenly: mostly forward, with a little back. Lower
// sharpness, for light scattered many times, spreads it more evenly. The forward lobe is kept broad and below a
// ceiling, so the thin edges of clouds in front of the sun glow rather than burn out into white outlines.
float GetCloudPhase(float cosAngle, float sharpness) {
    return min(lerp(GetPhase(cosAngle, 0.45 * sharpness), GetPhase(cosAngle, -0.3 * sharpness), 0.3), MaxPhase);
}

// How high above the ground a point along the ray is, in meters, without the rounding of differences of radii.
float GetAltitude(float distance, float mu) {
    const float height = layer.x;
    const float r = CloudsPlanetRadius + height;
    const float squared = height * (2.0 * CloudsPlanetRadius + height) + 2.0 * r * mu * distance + distance * distance;
    return squared / (sqrt(r * r + 2.0 * r * mu * distance + distance * distance) + CloudsPlanetRadius);
}

float4 main(Input input) : SV_Target0 {
    const float3 direction = GetRayDirection(input.uv);
    // How many radians of the view a pixel of this image spans.
    const float spread = length(ddx(direction)) + length(ddy(direction));
    const float coverage = cloudShape.x;
    const int2 pixel = min(int2(input.uv * viewSize.xy), int2(viewSize.xy) - 1);
    const float sceneDistance = GetSceneDistance(input.uv, sceneDepth.Load(int3(pixel, 0)));
    float2 segment;
    if (coverage <= 0.0 || !GetCloudSegment(layer.x, layer.y, layer.z, direction.y, segment) ||
        segment.x > min(sceneDistance, MaxStart)) {
        return 0.0;
    }
    segment.y = min(min(segment.y, sceneDistance), segment.x + MaxMarch);
    const float span = segment.y - segment.x;
    // Inside the layer, what is near fills most of the view, so the steps start short and grow away from the camera.
    const bool inside = segment.x <= 0.0;
    const float size = cloudShape.w;

    float3 toLight;
    const float3 light = GetCloudLight(toLight);
    const float cosAngle = dot(direction, toLight);
    // Light scattered several times inside a cloud reaches deeper, and spreads more evenly (Wrenninge 2013).
    const float3 phases =
        float3(GetCloudPhase(cosAngle, 1.0), GetCloudPhase(cosAngle, 0.5), GetCloudPhase(cosAngle, 0.25));
    const float3 ground = groundColor.rgb * groundLight.rgb;
    // The sky toward the horizon in the ray's direction, which far clouds take on as the air between them and the
    // camera thickens, losing their contrast before they fade into it.
    float3 skyTransmittance;
    const float3 hazeLight =
        GetSkyLight(normalize(float3(direction.x, max(direction.y, 0.02), direction.z)), sunDirection.xyz,
                    skyTransmittance);
    const float reach = (layer.z - layer.y) * LightReach;
    // Interleaved gradient noise, turned each frame, so the steps the rays skip differ from frame to frame.
    const float jitter =
        frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))) + volume.y * 0.618034);

    float transmittance = 1.0;
    float3 scattered = 0.0;
    float opacity = 0.0;
    float weightedDistance = 0.0;
    float weight = 0.0;
    for (int i = 0; i < STEPS; ++i) {
        const float along = (i + jitter) / STEPS;
        const float distance = segment.x + span * (inside ? along * along : along);
        const float stepLength = span * (inside ? 2.0 * max(along, 0.5 / STEPS) : 1.0) / STEPS;
        const float altitude = GetAltitude(distance, direction.y);
        const float2 position = (view.xz + direction.xz * distance - cloudMotion.xy) / size;
        // The clouds a pixel spans there, which picks the noise's mip level and fades the billows out.
        const float footprint = distance * spread / size;
        const float lod = log2(max(footprint * cloudMotion.w, 1.0));
        const float density = GetCloudDensity(position, altitude, lod, footprint, true);
        if (density <= 0.0) {
            continue;
        }
        // How much cloud lies between the point and the light, in steps growing away from it.
        float lightDepth = 0.0;
        float previous = 0.0;
        for (int j = 1; j <= LIGHT_STEPS; ++j) {
            const float farther = reach * (j * j) / (LIGHT_STEPS * LIGHT_STEPS);
            const float middle = (previous + farther) * 0.5;
            lightDepth += GetCloudDensity(position + toLight.xz * middle / size, altitude + toLight.y * middle,
                                          lod + 1.0, footprint * 2.0, false) *
                          (farther - previous);
            previous = farther;
        }
        lightDepth *= Extinction * LightExtinction * cloudShape.y;
        const float3 octaves = exp(-lightDepth * float3(1.0, 0.25, 0.0625)) * float3(1.0, 0.5, 0.3);
        const float sigma = density * cloudShape.y * Extinction;
        // Seen away from the light, the sunlit side darkens in its folds, where little cloud has scattered light
        // toward the camera yet ("powder", Schneider 2015).
        const float folds = 1.0 - exp(-2.0 * (lightDepth + sigma * 60.0));
        const float powder = lerp(1.0, folds, 0.6 * saturate(0.5 - 0.5 * cosAngle));
        const float height = saturate((altitude - layer.y) / (layer.z - layer.y));
        // The sky lights their tops, and the ground, more faintly, their bases, which the cloud above shades a little;
        // dense pockets let less of it in than thin ones. Sunlight scattered about the clouds adds to it, so no side
        // of a cloud by day is black.
        const float3 ambient = (lerp(ground, skylight.rgb, height) * lerp(0.75, 1.0, height) + light * BouncedSunlight) *
                               lerp(1.0, 0.8, density);
        const float air = distance * max(settings.y, 0.05) / HazeDistance;
        const float3 radiance =
            lerp(cloudColor.rgb * (light * dot(octaves, phases) * powder + ambient), hazeLight, 1.0 - exp(-air * 1.2));

        const float stepTransmittance = exp(-sigma * stepLength);
        const float share = transmittance * (1.0 - stepTransmittance);
        // Farther still, they fade into the haze, which the sky behind them shows: slowly at first, so the near ones
        // keep their shape while taking on its color, and to nothing before the farthest the rays reach.
        const float haze = exp(-0.6 * air * air) * (1.0 - smoothstep(MaxStart * 0.6, MaxStart, distance));
        scattered += radiance * share * haze;
        opacity += share * haze;
        weightedDistance += distance * share;
        weight += share;
        transmittance *= stepTransmittance;
        if (transmittance < 0.01) {
            break;
        }
    }
    if (opacity <= 0.0) {
        return 0.0;
    }
    float3 color = Expose(scattered / opacity);
    color = lerp(color, fogColor.rgb, GetFogAmount(direction * (weightedDistance / weight)));
    return float4(color * opacity, opacity);
}
