Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState baseColorSampler : register(s0, space2);
// The shadow cascades of the directional light, and the tiles of the other lights' shadows.
Texture2D<float> cascadeShadows : register(t1, space2);
SamplerComparisonState cascadeShadowSampler : register(s1, space2);
Texture2D<float> localShadows : register(t2, space2);
SamplerComparisonState localShadowSampler : register(s2, space2);
// The material's other textures, white where it has none.
Texture2D<float4> metallicRoughnessTexture : register(t3, space2);
SamplerState metallicRoughnessSampler : register(s3, space2);
Texture2D<float4> normalTexture : register(t4, space2);
SamplerState normalSampler : register(s4, space2);
Texture2D<float4> occlusionTexture : register(t5, space2);
SamplerState occlusionSampler : register(s5, space2);
Texture2D<float4> emissiveTexture : register(t6, space2);
SamplerState emissiveSampler : register(s6, space2);

// Must match Renderer3D::MaxLights, Detail::MaxShadowCascades and Renderer3D::MaxShadowTiles.
#define MAX_LIGHTS 16
#define MAX_CASCADES 4
#define MAX_SHADOW_TILES 32

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

cbuffer Lighting : register(b0, space3) {
    float4 ambientColor;
    float4 cameraPosition;
    uint4 lightCount;
    Light lights[MAX_LIGHTS];
};

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
};

cbuffer Material : register(b1, space3) {
    float4 baseColor;
    // Light the surface gives off itself.
    float4 emissive;
    float metallic;
    float roughness;
    // 0 when there is no normal map.
    float normalScale;
    float occlusionStrength;
    // 1 for double-sided materials.
    float doubleSided;
    // Positive to project the textures from world space, repeating every this many units, instead of using the UVs.
    float worldTextureSize;
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

struct Input {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float4 tangent : TEXCOORD3;
};

// Projects along the world axis the surface faces most, with texture rows going down on walls like on screen.
float2 GetWorldUv(float3 position, float3 normal) {
    const float3 facing = abs(normal);
    const float3 p = position / worldTextureSize;
    if (facing.x >= facing.y && facing.x >= facing.z) {
        return float2(p.z, -p.y);
    }
    if (facing.y >= facing.z) {
        return p.xz;
    }
    return float2(p.x, -p.y);
}

// Shadow edges are filtered over 3x3 comparisons, each blending the four nearest texels, which smooths them over about
// four texels. Surfaces beyond the tile's view are lit, so nothing outside a shadow map is shadowed by it.
float SampleShadow(Texture2D<float> map, SamplerComparisonState mapSampler, float4x4 view, float4 layout, uint tile,
                   float3 position) {
    const float4 clip = mul(view, float4(position, 1.0));
    const float3 projected = clip.xyz / clip.w;
    if (clip.w <= 0.0 || any(projected.xy < 0.0) || any(projected.xy > 1.0) || projected.z > 1.0) {
        return 1.0;
    }
    // Filtering stays inside the tile, which views draw with a margin for it.
    const float2 margin = 1.5 * layout.zw * layout.xy;
    const float2 tileUv = clamp(projected.xy, margin, 1.0 - margin);
    const float2 uv = (float2(tile % (uint)layout.x, tile / (uint)layout.x) + tileUv) / layout.xy;
    float lit = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            lit += map.SampleCmpLevelZero(mapSampler, uv + float2(x, y) * layout.zw, projected.z);
        }
    }
    return lit / 9.0;
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
// it reflects toward the viewer as highlights.
Lit GetLight(Light light, float3 position, float3 normal, float3 toViewer, float alpha, float3 reflectance) {
    const float type = light.positionAndType.w;
    float3 toLight = -light.directionAndRange.xyz;
    float attenuation = 1.0;
    if (type > 0.5) {
        const float3 offset = light.positionAndType.xyz - position;
        const float distance = length(offset);
        toLight = offset / max(distance, 0.0001);
        // Fades smoothly to nothing at the range, so a light can be skipped beyond it without a visible edge.
        const float fade = saturate(1.0 - pow(distance / light.directionAndRange.w, 2.0));
        attenuation = fade * fade;
        if (type > 1.5) {
            const float cone = saturate((dot(-toLight, light.directionAndRange.xyz) - light.cone.x) * light.cone.y);
            attenuation *= cone * cone;
        }
    }
    const float facing = dot(normal, toLight);
    if (light.cone.z >= 0.0 && attenuation > 0.0 && (facing > 0.0 || doubleSided > 0.5)) {
        // Back faces of double-sided surfaces are offset toward the light too.
        const float3 lightSide = facing < 0.0 ? -normal : normal;
        attenuation *= type > 0.5 ? GetLocalShadow(light, position, lightSide) : GetDirectionalShadow(position, lightSide);
    }
    const float3 color = light.color.rgb * attenuation;
    // Back faces of double-sided surfaces have normals pointing away from the viewer, so they are lit as if flipped.
    const float diffuse = doubleSided > 0.5 ? abs(facing) : max(facing, 0.0);
    // Only the side the viewer sees has highlights, and only from lights in front of it.
    const float3 seenNormal = dot(normal, toViewer) < 0.0 && doubleSided > 0.5 ? -normal : normal;
    Lit lit;
    lit.diffuse = color * diffuse;
    lit.specular = color * GetHighlight(seenNormal, toLight, toViewer, alpha, reflectance);
    return lit;
}

// Tilts the normal as the normal map says, in the tangent space of the surface. Projected textures have no tangents
// of their own, so they keep the mesh's normals.
float3 ApplyNormalMap(float3 normal, float4 tangent, float2 uv) {
    if (normalScale <= 0.0 || worldTextureSize > 0.0 || dot(tangent.xyz, tangent.xyz) < 1e-8) {
        return normal;
    }
    // Interpolation bends the tangent away from the normal, so it is made perpendicular again.
    const float3 t = normalize(tangent.xyz - normal * dot(normal, tangent.xyz));
    const float3 b = cross(normal, t) * (tangent.w < 0.0 ? -1.0 : 1.0);
    float3 mapped = normalTexture.Sample(normalSampler, uv).xyz * 2.0 - 1.0;
    mapped.xy *= normalScale;
    return normalize(t * mapped.x + b * mapped.y + normal * mapped.z);
}

float4 main(Input input) : SV_Target0 {
    const float3 surfaceNormal = normalize(input.normal);
    const float2 uv = worldTextureSize > 0.0 ? GetWorldUv(input.worldPosition, surfaceNormal) : input.uv;
    const float3 normal = ApplyNormalMap(surfaceNormal, input.tangent, uv);
    const float4 albedo = baseColorTexture.Sample(baseColorSampler, uv) * baseColor;
    const float4 metallicRoughness = metallicRoughnessTexture.Sample(metallicRoughnessSampler, uv);
    const float metalness = saturate(metallic * metallicRoughness.b);
    // Roughness is perceptually linear, and its square is GGX's alpha. Perfect mirrors would have highlights a pixel
    // wide and far too bright, which flicker.
    const float perceived = max(saturate(roughness * metallicRoughness.g), 0.05);
    const float alpha = perceived * perceived;
    const float occlusion = lerp(1.0, occlusionTexture.Sample(occlusionSampler, uv).r, occlusionStrength);
    // Dielectrics scatter their color and reflect about 4% of the light untinted, as plastic or varnish do; metals
    // scatter nothing and reflect in their color.
    const float3 scattered = albedo.rgb * (1.0 - metalness);
    const float3 reflectance = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metalness);
    const float3 toViewer = normalize(cameraPosition.xyz - input.worldPosition);
    float3 diffuse = 0.0;
    float3 highlights = 0.0;
    for (uint i = 0; i < lightCount.x; ++i) {
        const Lit lit = GetLight(lights[i], input.worldPosition, normal, toViewer, alpha, reflectance);
        diffuse += lit.diffuse;
        highlights += lit.specular;
    }
    const float3 emitted = emissive.rgb * emissiveTexture.Sample(emissiveSampler, uv).rgb;
    const float3 color = scattered * (diffuse + ambientColor.rgb * occlusion) + highlights + emitted;
    return float4(lerp(color, fogColor.rgb, GetFogAmount(input.worldPosition - fogCamera.xyz)), albedo.a);
}
