// How Renderer3D lights a surface, for its own mesh shader and for the fragment shaders of custom surfaces
// (Renderer3D::DrawSurface()), which it draws in the same pass with the same lights, shadows, sky and fog bound.
//
// Including it declares every resource of that pass, at the registers Renderer3D binds them to. Five texture slots are
// the surface's own: surfaceTexture0 (t0) and surfaceTexture1 to 4 (t3 to t6), white until bound. The fragment uniforms
// at b1 of space3 are the surface's own too, and a shader including this declares them, even if it reads none, since
// the slots of a stage are numbered without gaps.
#ifndef SPOT_SURFACE_LIGHTING_HLSL
#define SPOT_SURFACE_LIGHTING_HLSL

#include "environment.hlsl"

Texture2D<float4> surfaceTexture0 : register(t0, space2);
SamplerState surfaceSampler0 : register(s0, space2);
// The shadow cascades of the directional light, and the tiles of the other lights' shadows.
Texture2D<float> cascadeShadows : register(t1, space2);
SamplerComparisonState cascadeShadowSampler : register(s1, space2);
Texture2D<float> localShadows : register(t2, space2);
SamplerComparisonState localShadowSampler : register(s2, space2);
Texture2D<float4> surfaceTexture1 : register(t3, space2);
SamplerState surfaceSampler1 : register(s3, space2);
Texture2D<float4> surfaceTexture2 : register(t4, space2);
SamplerState surfaceSampler2 : register(s4, space2);
Texture2D<float4> surfaceTexture3 : register(t5, space2);
SamplerState surfaceSampler3 : register(s5, space2);
Texture2D<float4> surfaceTexture4 : register(t6, space2);
SamplerState surfaceSampler4 : register(s6, space2);
// The sky's light, blurred for each roughness down its mip levels, and its irradiance as nine spherical harmonics,
// already convolved with the cosine lobe.
TextureCube<float4> skyReflections : register(t7, space2);
SamplerState skyReflectionsSampler : register(s7, space2);
Texture2D<float4> skyIrradiance : register(t8, space2);
SamplerState skyIrradianceSampler : register(s8, space2);
// The depth of the highest surfaces around the camera, seen from straight above, which tells what lies under cover
// from the sky.
Texture2D<float> skyOcclusion : register(t9, space2);
SamplerComparisonState skyOcclusionSampler : register(s9, space2);
// The view's ambient occlusion, by pixel, white where it is off.
Texture2D<float> screenOcclusion : register(t10, space2);
SamplerState screenOcclusionSampler : register(s10, space2);
// The reflection probes' light: their reflections, blurred for each roughness, in tiles of an atlas (see
// GetProbeReflection()), and their irradiance as nine spherical harmonics, a row per probe, as the sky's.
Texture2D<float4> probeReflections : register(t11, space2);
SamplerState probeReflectionsSampler : register(s11, space2);
Texture2D<float4> probeIrradiance : register(t12, space2);
SamplerState probeIrradianceSampler : register(s12, space2);

// Must match Detail::MaxShadowCascades and Renderer3D::MaxShadowTiles.
#define MAX_CASCADES 4
#define MAX_SHADOW_TILES 32
// Must match Detail::ProbeLevels.
#define PROBE_LEVELS 5

struct Light {
    // w is the type: 0 for directional, 1 for point and 2 for spot.
    float4 positionAndType;
    // The normalized direction the light travels in, and the range.
    float4 directionAndRange;
    // Premultiplied by the intensity.
    float4 color;
    // The cosine of the outer cone angle, and the inverse of how much greater the inner one's is. Then, for lights
    // that cast shadows, 0 or more: for directional lights, any such value means the cascades are theirs; for the
    // others, the first of their shadow tiles, and in w the size of their texels per unit of distance.
    float4 cone;
};

struct Probe {
    // The box it lights, with in w how far inside it its light fades in, and its intensity.
    float4 boxMin;
    float4 boxMax;
    // Where it captured the scene from, and in w its tile in the atlas.
    float4 position;
};

// The frame's lights, directional ones first; the view's clusters, each the first of its lights in lightIndices and
// how many there are; and those lists of lights, by their index in lights. See Detail::BuildLightClusters(). Then the
// reflection probes, the smallest first, so a probe inside another lights what it holds.
StructuredBuffer<Light> lights : register(t13, space2);
StructuredBuffer<uint2> clusters : register(t14, space2);
StructuredBuffer<uint> lightIndices : register(t15, space2);
StructuredBuffer<Probe> probes : register(t16, space2);

cbuffer Lighting : register(b0, space3) {
    float4 ambientColor;
    float4 cameraPosition;
    // x is the number of directional lights, which reach every pixel; y the number of reflection probes.
    uint4 lightCount;
    // 1 in x when the sky lights the scene, and in y the mip level of its reflections for a roughness of 1.
    float4 skyLighting;
    // The clusters across, down and in depth.
    uint4 clusterGrid;
    // The near plane's depth and the scale of the slices' logarithm, and the size of the view in pixels.
    float4 clusterDepth;
    // The camera's forward axis, along which depth goes.
    float4 viewForward;
    // The size of a probe's tile at its first level, the tiles across the atlas, and the inverse of its size.
    float4 probeAtlas;
    // What lights a point in place of the sky it is covered from: the ground's light, reflected again by the underside
    // of what covers it.
    float4 skyBounce;
};

// The list of lights that may reach the pixel at the position, at the depth along the camera's forward axis.
uint2 GetCluster(float2 pixel, float depth) {
    const uint2 tile = min(uint2(pixel / clusterDepth.zw * float2(clusterGrid.xy)), clusterGrid.xy - 1);
    const float slice = depth > clusterDepth.x ? floor(log(depth / clusterDepth.x) * clusterDepth.y) : 0.0;
    const uint z = (uint)clamp(slice, 0.0, float(clusterGrid.z - 1));
    return clusters[(z * clusterGrid.y + tile.y) * clusterGrid.x + tile.x];
}

// Shadow views take world positions to the [0, 1] UVs of their tile, Y down, and to the depth it holds.
cbuffer Shadows : register(b2, space3) {
    float4x4 cascadeViews[MAX_CASCADES];
    float4x4 tileViews[MAX_SHADOW_TILES];
    // The view distance at which each cascade ends.
    float4 cascadeEnds;
    // The size of a texel of each cascade in world units.
    float4 cascadeTexelSizes;
    // The camera's forward direction, and the number of cascades.
    float4 cameraForward;
    // The columns and rows of tiles in each shadow map, and the size of their texels in UVs.
    float4 cascadeLayout;
    float4 tileLayout;
    // Takes world positions to the UVs of the sky's occlusion, Y down, and to the depth it holds, which grows
    // downward.
    float4x4 skyOcclusionView;
    // 1 in x when there is sky occlusion; the radius around a point it looks for cover in, in world units; the depth a
    // world unit of height spans; and the size of its texels in world units.
    float4 skyOcclusionParams;
};

// The fog's color, with 1 in w when there is fog; its density, start, height falloff and base height; and the camera's
// position.
cbuffer Fog : register(b3, space3) {
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

// Points spread evenly over a disc (a Poisson disk), at which shadow maps are compared around each surface. The first
// four are the outermost, one per quadrant.
static const float2 ShadowTaps[16] = {
    float2(0.9456, -0.7689),  float2(-0.8154, -0.8791), float2(0.9748, 0.7565),   float2(-0.8141, 0.9144),
    float2(-0.9420, -0.3991), float2(-0.0942, -0.9294), float2(0.3450, 0.2939),   float2(-0.9159, 0.4577),
    float2(-0.3828, 0.2768),  float2(0.4432, -0.9751),  float2(0.5374, -0.4737),  float2(-0.2650, -0.4189),
    float2(0.7920, 0.1909),   float2(-0.2419, 0.9971),  float2(0.1998, 0.7864),   float2(0.1438, -0.1410)};

// Shadow edges are filtered over 16 comparisons on a disc a few texels wide, each blending the four nearest texels,
// so they fade smoothly over about four texels without the grid of a regular pattern; the disc is the same for every
// pixel, so edges do not flicker. Pixels whose four outermost comparisons agree take only those. Surfaces beyond the
// tile's view are lit, so nothing outside a shadow map is shadowed by it.
float SampleShadow(Texture2D<float> map, SamplerComparisonState mapSampler, float4x4 view, float4 layout, uint tile,
                   float3 position) {
    const float4 clip = mul(view, float4(position, 1.0));
    const float3 projected = clip.xyz / clip.w;
    if (clip.w <= 0.0 || any(projected.xy < 0.0) || any(projected.xy > 1.0) || projected.z > 1.0) {
        return 1.0;
    }
    // Filtering stays inside the tile, which views draw with a margin of three texels for it.
    const float2 margin = 3.0 * layout.zw * layout.xy;
    const float2 tileUv = clamp(projected.xy, margin, 1.0 - margin);
    const float2 uv = (float2(tile % (uint)layout.x, tile / (uint)layout.x) + tileUv) / layout.xy;
    // Where the outermost taps agree, the surface is wholly lit or wholly in shadow, as most are, and the others
    // would change next to nothing.
    float lit = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        lit += map.SampleCmpLevelZero(mapSampler, uv + ShadowTaps[i] * 1.5 * layout.zw, projected.z);
    }
    if (lit <= 0.0 || lit >= 4.0) {
        return lit / 4.0;
    }
    [unroll] for (int j = 4; j < 16; ++j) {
        lit += map.SampleCmpLevelZero(mapSampler, uv + ShadowTaps[j] * 1.5 * layout.zw, projected.z);
    }
    return lit / 16.0;
}

// Surfaces are moved toward the light along their normal by about a texel of the shadow map before they are looked up
// in it, which keeps them from shadowing themselves where its texels, larger than the surface's pixels, cover depths
// both in front of and behind it.
float3 OffsetForShadow(float3 position, float3 normal, float texelSize) {
    return position + normal * texelSize * 1.5;
}

float GetCascadeShadow(uint cascade, float3 position, float3 normal) {
    return SampleShadow(cascadeShadows, cascadeShadowSampler, cascadeViews[cascade], cascadeLayout, cascade,
                        OffsetForShadow(position, normal, cascadeTexelSizes[cascade]));
}

// Each cascade blends into the next over its last tenth, and the last one fades out into no shadow.
float GetDirectionalShadow(float3 position, float3 normal) {
    const float distance = dot(position - cameraPosition.xyz, cameraForward.xyz);
    const uint count = (uint)cameraForward.w;
    uint cascade = 0;
    while (cascade + 1 < count && distance > cascadeEnds[cascade]) {
        ++cascade;
    }
    const float end = cascadeEnds[cascade];
    if (distance >= end) {
        return 1.0;
    }
    const float start = cascade == 0 ? 0.0 : cascadeEnds[cascade - 1];
    const float band = 0.1 * (end - start);
    const float blend = saturate((distance - (end - band)) / band);
    float lit = GetCascadeShadow(cascade, position, normal);
    if (blend > 0.0) {
        const float next = cascade + 1 < count ? GetCascadeShadow(cascade + 1, position, normal) : 1.0;
        lit = lerp(lit, next, blend);
    }
    return lit;
}

// Point lights have six tiles, one per face of a cube around them, in the order +X, -X, +Y, -Y, +Z and -Z.
float GetLocalShadow(Light light, float3 position, float3 normal) {
    const float3 fromLight = position - light.positionAndType.xyz;
    uint tile = (uint)light.cone.z;
    if (light.positionAndType.w < 1.5) {
        const float3 axis = abs(fromLight);
        if (axis.x >= axis.y && axis.x >= axis.z) {
            tile += fromLight.x > 0.0 ? 0 : 1;
        } else if (axis.y >= axis.z) {
            tile += fromLight.y > 0.0 ? 2 : 3;
        } else {
            tile += fromLight.z > 0.0 ? 4 : 5;
        }
    }
    const float texelSize = light.cone.w * length(fromLight);
    return SampleShadow(localShadows, localShadowSampler, tileViews[tile], tileLayout, tile,
                        OffsetForShadow(position, normal, texelSize));
}

struct Lit {
    float3 diffuse;
    float3 specular;
};

static const float Pi = 3.14159265;

// The light a surface reflects toward the viewer, as a share of the light reaching it: GGX microfacets, with
// height-correlated Smith visibility and Schlick's Fresnel. It is scaled by pi, as lights' colors are the light a
// white surface facing them scatters rather than what reaches it.
float3 GetHighlight(float3 normal, float3 toLight, float3 toViewer, float alpha, float3 reflectance) {
    const float nl = saturate(dot(normal, toLight));
    if (nl <= 0.0) {
        return 0.0;
    }
    const float nv = max(dot(normal, toViewer), 1e-4);
    const float3 halfway = normalize(toLight + toViewer);
    const float nh = saturate(dot(normal, halfway));
    const float a2 = alpha * alpha;
    const float d = nh * nh * (a2 - 1.0) + 1.0;
    const float distribution = a2 / (Pi * d * d);
    const float visibility = 0.5 / (nl * sqrt(nv * nv * (1.0 - a2) + a2) + nv * sqrt(nl * nl * (1.0 - a2) + a2));
    const float3 fresnel = reflectance + (1.0 - reflectance) * pow(1.0 - saturate(dot(toViewer, halfway)), 5.0);
    return Pi * distribution * visibility * fresnel * nl;
}

// The light a surface at the position with the normal scatters, before it takes the surface's color, and the light
// it reflects toward the viewer as highlights. Double-sided surfaces are lit from either side.
//
// Directional lights come first in the frame's lights and clusters hold only the others, so each loop over them says
// which kind it goes through as a constant, and leaves out the shadows and falloff of the other kind.
Lit GetLightOfKind(Light light, float3 position, float3 normal, float3 toViewer, float alpha, float3 reflectance,
                   bool doubleSided, bool local) {
    const float type = light.positionAndType.w;
    float3 toLight = -light.directionAndRange.xyz;
    float attenuation = 1.0;
    if (local) {
        const float3 offset = light.positionAndType.xyz - position;
        const float distanceSquared = dot(offset, offset);
        toLight = offset * rsqrt(max(distanceSquared, 1e-8));
        // Inverse-square falloff, windowed to reach nothing at the range without a visible edge, so a light can be
        // skipped beyond it. Surfaces closer than a centimeter are lit as if at a centimeter, not infinitely.
        const float ratio = distanceSquared / (light.directionAndRange.w * light.directionAndRange.w);
        const float window = saturate(1.0 - ratio * ratio);
        attenuation = window * window / max(distanceSquared, 1e-4);
        if (type > 1.5) {
            const float cone = saturate((dot(-toLight, light.directionAndRange.xyz) - light.cone.x) * light.cone.y);
            attenuation *= cone * cone;
        }
        // Clusters hold every light that may reach them, so many pixels are beyond the reach of most of theirs.
        if (attenuation <= 0.0) {
            return (Lit)0;
        }
    }
    const float facing = dot(normal, toLight);
    if (light.cone.z >= 0.0 && attenuation > 0.0 && (facing > 0.0 || doubleSided)) {
        // Back faces of double-sided surfaces are offset toward the light too.
        const float3 lightSide = facing < 0.0 ? -normal : normal;
        attenuation *= local ? GetLocalShadow(light, position, lightSide) : GetDirectionalShadow(position, lightSide);
    }
    const float3 color = light.color.rgb * attenuation;
    // Back faces of double-sided surfaces have normals pointing away from the viewer, so they are lit as if flipped.
    const float diffuse = doubleSided ? abs(facing) : max(facing, 0.0);
    // Only the side the viewer sees has highlights, and only from lights in front of it.
    const float3 seenNormal = dot(normal, toViewer) < 0.0 && doubleSided ? -normal : normal;
    Lit lit;
    lit.diffuse = color * diffuse;
    lit.specular = color * GetHighlight(seenNormal, toLight, toViewer, alpha, reflectance);
    return lit;
}

Lit GetLight(Light light, float3 position, float3 normal, float3 toViewer, float alpha, float3 reflectance,
             bool doubleSided) {
    return GetLightOfKind(light, position, normal, toViewer, alpha, reflectance, doubleSided,
                          light.positionAndType.w > 0.5);
}

// The light a surface facing the direction receives from what the nine spherical harmonics in the row hold.
float3 GetIrradiance(Texture2D<float4> harmonics, int row, float3 n) {
    float3 sh[9];
    [unroll] for (int i = 0; i < 9; ++i) {
        sh[i] = harmonics.Load(int3(i, row, 0)).rgb;
    }
    return max(sh[0] * 0.282095 + (sh[1] * n.y + sh[2] * n.z + sh[3] * n.x) * 0.488603 +
                   (sh[4] * n.x * n.y + sh[5] * n.y * n.z + sh[7] * n.x * n.z) * 1.092548 +
                   sh[6] * 0.315392 * (3.0 * n.z * n.z - 1.0) + sh[8] * 0.546274 * (n.x * n.x - n.y * n.y),
               0.0);
}

// The light a surface facing the direction receives from the sky.
float3 GetSkyIrradiance(float3 n) {
    return GetIrradiance(skyIrradiance, 0, n);
}

// Whether nothing is higher than the point the offset moves to across the sky's occlusion, from 0 to 1.
float GetSkyTap(float3 projectedCenter, float3 projectedX, float3 projectedZ, float3 normal, float2 offset, float bias) {
    const float rise = normal.y > 0.3 ? max(-(normal.x * offset.x + normal.z * offset.y) / normal.y, 0.0) : 0.0;
    const float3 projected = projectedCenter + projectedX * offset.x + projectedZ * offset.y;
    if (any(projected.xy < 0.0) || any(projected.xy > 1.0)) {
        return 1.0;
    }
    const float depth = projected.z - (rise + bias) * skyOcclusionParams.z;
    return skyOcclusion.SampleCmpLevelZero(skyOcclusionSampler, projected.xy, depth);
}

// How much of the sky a point sees past what covers it, from 0 under a roof or in a cave to 1 in the open: the share
// of a disc around it, in the sky's occlusion, where no surface is higher than it. The disc is moved off the surface
// the way it faces along the ground, so walls look into the air in front of them, but not up, so a floor still finds
// the ceiling over it however low; and a sloping surface counts as higher only what rises above its own slope, so
// hillsides do not hide themselves.
float GetSkyVisibility(float3 position, float3 normal) {
    if (skyOcclusionParams.x < 0.5) {
        return 1.0;
    }
    const float radius = skyOcclusionParams.y;
    const float3 center = position + float3(normal.x, 0.0, normal.z) * radius;
    // A couple of texels, so surfaces that are the highest where they are are not hidden by their own depth.
    const float bias = 2.0 * skyOcclusionParams.w + 0.05;
    // The view is orthographic, so each tap's projection is the center's moved along the view's axes.
    const float3 projectedCenter = mul(skyOcclusionView, float4(center, 1.0)).xyz;
    const float3 projectedX = mul(skyOcclusionView, float4(1.0, 0.0, 0.0, 0.0)).xyz;
    const float3 projectedZ = mul(skyOcclusionView, float4(0.0, 0.0, 1.0, 0.0)).xyz;
    // Where the outermost taps agree, as in the open or deep under cover, the others would change nothing.
    float visible = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        visible += GetSkyTap(projectedCenter, projectedX, projectedZ, normal, ShadowTaps[i] * radius, bias);
    }
    if (visible <= 0.0 || visible >= 4.0) {
        return visible / 4.0;
    }
    [unroll] for (int j = 4; j < 16; ++j) {
        visible += GetSkyTap(projectedCenter, projectedX, projectedZ, normal, ShadowTaps[j] * radius, bias);
    }
    return visible / 16.0;
}

// The view's ambient occlusion at the pixel, SV_Position.xy, from 1 for none. It is worked out from the camera's
// depth, which holds only what hides what is behind it, so transparent surfaces leave it out.
float GetScreenOcclusion(float2 pixel) {
    return screenOcclusion.SampleLevel(screenOcclusionSampler, pixel / clusterDepth.zw, 0.0);
}

// How much the probe lights the position: fully inside its box, fading in over its blend distance from the box's
// faces, and not at all outside it.
float GetProbeWeight(Probe probe, float3 position) {
    const float3 inside = min(position - probe.boxMin.xyz, probe.boxMax.xyz - position);
    const float depth = min(inside.x, min(inside.y, inside.z));
    return depth <= 0.0 ? 0.0 : saturate(depth / max(probe.boxMin.w, 1e-4));
}

// The probe's reflections at one of their levels, in the tile's place in the atlas: the first level fills the top of
// the tile, a square, and each of the next, half the size of the one before, follows the one before under it, left to
// right. Each square has a border of a texel that repeats its edge.
float3 GetProbeLevel(uint tile, float3 direction, float level) {
    const float size = probeAtlas.x;
    const uint columns = (uint)probeAtlas.y;
    const float2 tileOrigin = float2(tile % columns, tile / columns) * float2(size, size * 1.5);
    const float levelSize = size / exp2(level);
    const float2 levelOrigin = level < 0.5 ? float2(0.0, 0.0) : float2(size - 2.0 * levelSize, size);
    const float2 texel = tileOrigin + levelOrigin + 1.0 + EncodeOctahedral(direction) * (levelSize - 2.0);
    return probeReflections.SampleLevel(probeReflectionsSampler, texel * probeAtlas.zw, 0.0).rgb;
}

// What the probe reflects along the direction from the position, blurred for the roughness. Probes capture what is
// around them as if it were infinitely far, so the direction is turned toward where it meets the probe's box, which
// stands for the walls of the room it lights (Lagarde and Zanuttini 2012).
float3 GetProbeReflection(Probe probe, float3 position, float3 direction, float roughness) {
    const float3 safe = (1.0 - 2.0 * step(direction, 0.0)) * max(abs(direction), 1e-5);
    const float3 exits = max((probe.boxMax.xyz - position) / safe, (probe.boxMin.xyz - position) / safe);
    const float3 projected = position + direction * min(exits.x, min(exits.y, exits.z)) - probe.position.xyz;
    const uint tile = (uint)probe.position.w;
    const float level = roughness * (PROBE_LEVELS - 1);
    const float lower = floor(level);
    return lerp(GetProbeLevel(tile, projected, lower), GetProbeLevel(tile, projected, min(lower + 1.0, PROBE_LEVELS - 1)),
                level - lower);
}

// The integral of the GGX lobe over the environment, as the scale and bias of the reflectance that give the share of
// light from all around a surface reflects toward the viewer, fitted analytically (Karis 2014) instead of read from a
// table. Their sum is the share a surface reflecting everything keeps, when its light bounces off a single microfacet.
float2 GetEnvironmentBrdf(float perceived, float nv) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = perceived * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

// A point of a surface as lighting needs it.
struct SurfacePoint {
    float3 position;
    // Unit length, toward the outside of the surface.
    float3 normal;
    // SV_Position.xy, which finds the lights that may reach the surface.
    float2 pixel;
    // Lit from either side, as leaves and cloth are.
    bool doubleSided;
    // Perceptual roughness, from 0 for a mirror to 1 for a matte surface; at least 0.05.
    float roughness;
    // The light the surface reflects untinted at normal incidence, about 0.04 for dielectrics and their color for
    // metals.
    float3 reflectance;
};

// The light reaching a point: what it scatters before it takes the surface's color, its highlights, and the light from
// all around that it scatters and reflects, before occlusion darkens either.
struct SurfaceLight {
    float3 diffuse;
    float3 highlights;
    float3 surroundings;
    float3 reflectedSurroundings;
    // The share of reflectedSurroundings the surface reflects toward the viewer.
    float3 environmentReflectance;
};

SurfaceLight LightSurface(SurfacePoint surface) {
    const float alpha = surface.roughness * surface.roughness;
    const float3 toViewer = normalize(cameraPosition.xyz - surface.position);
    SurfaceLight light;
    light.diffuse = 0.0;
    light.highlights = 0.0;
    for (uint i = 0; i < lightCount.x; ++i) {
        const Lit lit = GetLightOfKind(lights[i], surface.position, surface.normal, toViewer, alpha,
                                       surface.reflectance, surface.doubleSided, false);
        light.diffuse += lit.diffuse;
        light.highlights += lit.specular;
    }
    const uint2 cluster = GetCluster(surface.pixel, dot(surface.position - cameraPosition.xyz, viewForward.xyz));
    for (uint j = 0; j < cluster.y; ++j) {
        const Lit lit = GetLightOfKind(lights[lightIndices[cluster.x + j]], surface.position, surface.normal, toViewer,
                                       alpha, surface.reflectance, surface.doubleSided, true);
        light.diffuse += lit.diffuse;
        light.highlights += lit.specular;
    }
    // Light from all around: ambient lights, the same from every direction; the reflection probes whose boxes hold the
    // point, scattered by their irradiance and reflected from their blurred captures; and the sky's light for what
    // they leave, likewise, as much as the point is not under cover.
    light.surroundings = ambientColor.rgb;
    light.reflectedSurroundings = ambientColor.rgb;
    const float3 seenNormal = dot(surface.normal, toViewer) < 0.0 && surface.doubleSided ? -surface.normal : surface.normal;
    const float3 mirrored = reflect(-toViewer, seenNormal);
    float remaining = 1.0;
    for (uint p = 0; p < lightCount.y && remaining > 0.0; ++p) {
        const Probe probe = probes[p];
        const float weight = GetProbeWeight(probe, surface.position) * remaining;
        if (weight > 0.0) {
            const float scale = weight * probe.boxMax.w;
            light.surroundings += GetIrradiance(probeIrradiance, (int)probe.position.w, seenNormal) * scale;
            light.reflectedSurroundings +=
                GetProbeReflection(probe, surface.position, mirrored, surface.roughness) * scale;
            remaining -= weight;
        }
    }
    // What covers a point from the sky is not black: lit from below, it sends back some of the ground's light.
    if (skyLighting.x > 0.5 && remaining > 0.0) {
        const float visible = GetSkyVisibility(surface.position, seenNormal);
        const float3 covered = skyBounce.rgb * ((1.0 - visible) * remaining);
        const float sky = visible * remaining;
        light.surroundings += GetSkyIrradiance(seenNormal) * sky + covered;
        light.reflectedSurroundings +=
            skyReflections.SampleLevel(skyReflectionsSampler, mirrored, surface.roughness * skyLighting.y).rgb * sky +
            covered;
    }
    const float nv = saturate(dot(seenNormal, toViewer));
    const float2 brdf = GetEnvironmentBrdf(surface.roughness, nv);
    const float3 singleScattering = surface.reflectance * brdf.x + brdf.y;
    // Light that bounces between microfacets before it leaves is lost to single scattering, which darkens rough
    // surfaces, rough metals most; it is added back as Fdez-Aguera (2019) and Filament do.
    const float kept = max(brdf.x + brdf.y, 0.05);
    const float3 average = surface.reflectance + (1.0 - surface.reflectance) / 21.0;
    light.highlights *= 1.0 + surface.reflectance * (1.0 / kept - 1.0);
    light.environmentReflectance =
        singleScattering + singleScattering * average * (1.0 - kept) / (1.0 - (1.0 - kept) * average);
    return light;
}

// How much of the light from all around a surface of the albedo scatters reaches it, past what darkens it (occlusion,
// from 1 for none). Light also bounces between the surfaces of a crease before it leaves, which occlusion alone leaves
// out, so bright creases stay lighter than dark ones: Jimenez et al.'s (2016) fit of that.
float3 GetDiffuseOcclusion(float occlusion, float3 albedo) {
    const float3 a = 2.0404 * albedo - 0.3324;
    const float3 b = -4.7951 * albedo + 0.6417;
    const float3 c = 2.7552 * albedo + 0.6903;
    return max(occlusion, ((occlusion * a + b) * occlusion + c) * occlusion);
}

// How much of the light from all around a surface reflects reaches it, past what darkens it (occlusion, from 1 for
// none) and the surface itself. Occlusion hides more of the reflections of smooth surfaces and at grazing angles
// (Lagarde 2014). Normal maps can tilt the mirror direction below the horizon of the surface they are on, whose
// geometric normal is surfaceNormal, where it would reflect the surface's own inside.
float GetReflectionOcclusion(SurfacePoint surface, float3 surfaceNormal, float occlusion) {
    const float3 toViewer = normalize(cameraPosition.xyz - surface.position);
    const bool backFace = surface.doubleSided && dot(surface.normal, toViewer) < 0.0;
    const float3 normal = backFace ? -surface.normal : surface.normal;
    const float3 horizonNormal = backFace ? -surfaceNormal : surfaceNormal;
    const float nv = saturate(dot(normal, toViewer));
    const float alpha = surface.roughness * surface.roughness;
    const float occluded = saturate(pow(nv + occlusion, exp2(-16.0 * alpha - 1.0)) - 1.0 + occlusion);
    const float horizon = saturate(1.0 + dot(reflect(-toViewer, normal), horizonNormal));
    return occluded * horizon * horizon;
}

// The color of an opaque surface as Renderer3D shades its meshes: lit by the frame's lights, shadows, probes and sky,
// giving off its emitted light, and fogged. Metalness and roughness are from 0 to 1, as in glTF; occlusion darkens the
// light from all around, from 1 for none, along with the view's ambient occlusion. The normal may be tilted by a normal
// map, and surfaceNormal is then the geometric normal it was tilted from.
float3 ShadeOpaqueSurface(float3 position, float3 normal, float3 surfaceNormal, float2 pixel, float3 albedo,
                          float metalness, float roughness, float occlusion, float3 emitted, bool doubleSided) {
    occlusion *= GetScreenOcclusion(pixel);
    SurfacePoint surface;
    surface.position = position;
    surface.normal = normal;
    surface.pixel = pixel;
    surface.doubleSided = doubleSided;
    // Perfect mirrors would have highlights a pixel wide and far too bright, which flicker.
    surface.roughness = max(saturate(roughness), 0.05);
    // Dielectrics scatter their color and reflect about 4% of the light untinted, as plastic or varnish do; metals
    // scatter nothing and reflect in their color.
    surface.reflectance = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
    const SurfaceLight light = LightSurface(surface);
    const float3 scattered = albedo * (1.0 - metalness);
    const float3 ambient = scattered * light.surroundings * GetDiffuseOcclusion(occlusion, scattered) +
                           light.reflectedSurroundings * light.environmentReflectance *
                               GetReflectionOcclusion(surface, surfaceNormal, occlusion);
    const float3 color = scattered * light.diffuse + ambient + light.highlights + emitted;
    return lerp(color, fogColor.rgb, GetFogAmount(position - fogCamera.xyz));
}

float3 ShadeOpaqueSurface(float3 position, float3 normal, float2 pixel, float3 albedo, float metalness, float roughness,
                          float occlusion, float3 emitted, bool doubleSided) {
    return ShadeOpaqueSurface(position, normal, normal, pixel, albedo, metalness, roughness, occlusion, emitted,
                              doubleSided);
}

#endif
