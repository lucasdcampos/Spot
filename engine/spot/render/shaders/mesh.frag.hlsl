#include "surface_lighting.hlsl"

// The material's textures, in the surface's own slots, white where it has none.
#define baseColorTexture surfaceTexture0
#define baseColorSampler surfaceSampler0
#define metallicRoughnessTexture surfaceTexture1
#define metallicRoughnessSampler surfaceSampler1
#define normalTexture surfaceTexture2
#define normalSampler surfaceSampler2
#define occlusionTexture surfaceTexture3
#define occlusionSampler surfaceSampler3
#define emissiveTexture surfaceTexture4
#define emissiveSampler surfaceSampler4

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
    // Positive to project the textures from world space, or the mesh's (see TextureSpace), repeating every this many
    // units, instead of using the UVs.
    float worldTextureSize;
    // 0 for opaque, 1 for cutout and 2 for transparent surfaces, and the alpha below which cutout pixels are dropped.
    float alphaMode;
    float alphaCutoff;
};

struct Input {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    // What projected textures are projected by, and the world directions of the axes they are projected along.
    float3 texturePosition : TEXCOORD4;
    float3 textureNormal : TEXCOORD5;
    float3 textureAxisX : TEXCOORD6;
    float3 textureAxisY : TEXCOORD7;
    float3 textureAxisZ : TEXCOORD8;
};

// Projects along the axis the surface faces most, of the world or of the mesh, with texture rows going down on walls
// like on screen.
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

// Tilts the normal as the normal map says, in the tangent space of the surface: along the mesh's tangents, or for
// projected textures along the axes GetWorldUv() lays them out on, with the bitangent toward lower v as on meshes.
float3 ApplyNormalMap(float3 normal, float4 tangent, float2 uv, float2 uvDx, float2 uvDy, Input input) {
    if (normalScale <= 0.0) {
        return normal;
    }
    float3 t;
    float3 b;
    if (worldTextureSize > 0.0) {
        const float3 facing = abs(input.textureNormal);
        if (facing.x >= facing.y && facing.x >= facing.z) {
            t = float3(0.0, 0.0, 1.0);
            b = float3(0.0, 1.0, 0.0);
        } else if (facing.y >= facing.z) {
            t = float3(1.0, 0.0, 0.0);
            b = float3(0.0, 0.0, -1.0);
        } else {
            t = float3(1.0, 0.0, 0.0);
            b = float3(0.0, 1.0, 0.0);
        }
        // Turned from the projection's axes to the world's, then laid onto the surface, which they lean out of on
        // slopes.
        const float3x3 axes = float3x3(input.textureAxisX, input.textureAxisY, input.textureAxisZ);
        t = mul(t, axes);
        b = mul(b, axes);
        t = normalize(t - normal * dot(normal, t));
        b = normalize(b - normal * dot(normal, b));
    } else {
        if (dot(tangent.xyz, tangent.xyz) < 1e-8) {
            return normal;
        }
        // Interpolation bends the tangent away from the normal, so it is made perpendicular again.
        t = normalize(tangent.xyz - normal * dot(normal, tangent.xyz));
        b = cross(normal, t) * (tangent.w < 0.0 ? -1.0 : 1.0);
    }
    float3 mapped = normalTexture.SampleGrad(normalSampler, uv, uvDx, uvDy).xyz * 2.0 - 1.0;
    mapped.xy *= normalScale;
    return normalize(t * mapped.x + b * mapped.y + normal * mapped.z);
}

float4 main(Input input) : SV_Target0 {
    const float3 surfaceNormal = normalize(input.normal);
    const float2 uv =
        worldTextureSize > 0.0 ? GetWorldUv(input.texturePosition, normalize(input.textureNormal)) : input.uv;
    // Preserve derivatives before alpha clipping so surviving leaf edges use the same normal-map mip levels.
    const float2 uvDx = ddx(uv);
    const float2 uvDy = ddy(uv);
    const float4 albedo = baseColorTexture.Sample(baseColorSampler, uv) * baseColor;
    if (alphaMode > 0.5 && alphaMode < 1.5 && albedo.a < alphaCutoff) {
        discard;
    }
    const float3 normal = ApplyNormalMap(surfaceNormal, input.tangent, uv, uvDx, uvDy, input);
    const float4 metallicRoughness = metallicRoughnessTexture.Sample(metallicRoughnessSampler, uv);
    const float metalness = saturate(metallic * metallicRoughness.b);
    const float occlusion = lerp(1.0, occlusionTexture.Sample(occlusionSampler, uv).r, occlusionStrength);
    const float3 emitted = emissive.rgb * emissiveTexture.Sample(emissiveSampler, uv).rgb;
    if (alphaMode < 1.5) {
        return float4(ShadeOpaqueSurface(input.worldPosition, normal, surfaceNormal, input.position.xy, albedo.rgb,
                                         metalness, roughness * metallicRoughness.g, occlusion, emitted,
                                         doubleSided > 0.5),
                      1.0);
    }

    SurfacePoint surface;
    surface.position = input.worldPosition;
    surface.normal = normal;
    surface.pixel = input.position.xy;
    surface.doubleSided = doubleSided > 0.5;
    surface.roughness = max(saturate(roughness * metallicRoughness.g), 0.05);
    surface.reflectance = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metalness);
    const SurfaceLight light = LightSurface(surface);
    const float3 scattered = albedo.rgb * (1.0 - metalness);
    const float fog = GetFogAmount(input.worldPosition - fogCamera.xyz);
    // Premultiplied: what the surface scatters covers what is behind it as much as its alpha says, while what it
    // reflects and gives off is added at full strength. Fog covers it all, as much as the surface is there.
    const float3 scatteredLight = scattered * (light.diffuse + light.surroundings * occlusion);
    const float3 reflectedLight = light.reflectedSurroundings * light.environmentReflectance *
                                      GetReflectionOcclusion(surface, surfaceNormal, occlusion) +
                                  light.highlights + emitted;
    const float3 premultiplied = scatteredLight * albedo.a + reflectedLight;
    return float4(lerp(premultiplied, fogColor.rgb * albedo.a, fog), albedo.a);
}
