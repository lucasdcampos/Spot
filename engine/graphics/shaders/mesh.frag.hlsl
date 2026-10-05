Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState baseColorSampler : register(s0, space2);

// Must match Renderer3D::MaxLights.
#define MAX_LIGHTS 16

struct Light {
    // w is the type: 0 for directional, 1 for point and 2 for spot.
    float4 positionAndType;
    // The normalized direction the light travels in, and the range.
    float4 directionAndRange;
    // Premultiplied by the intensity.
    float4 color;
    // The cosine of the outer cone angle, and the inverse of how much greater the inner one's is.
    float4 cone;
};

cbuffer Lighting : register(b0, space3) {
    float4 ambientColor;
    uint4 lightCount;
    Light lights[MAX_LIGHTS];
};

cbuffer Material : register(b1, space3) {
    float4 baseColor;
    // 1 for double-sided materials.
    float doubleSided;
    // Positive to project the texture from world space, repeating every this many units, instead of using the UVs.
    float worldTextureSize;
};

struct Input {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
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

// The light reaching a surface at the position with the normal.
float3 GetLight(Light light, float3 position, float3 normal) {
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
    // Back faces of double-sided surfaces have normals pointing away from the viewer, so they are lit as if flipped.
    const float diffuse = doubleSided > 0.5 ? abs(facing) : max(facing, 0.0);
    return light.color.rgb * diffuse * attenuation;
}

float4 main(Input input) : SV_Target0 {
    const float3 normal = normalize(input.normal);
    const float2 uv = worldTextureSize > 0.0 ? GetWorldUv(input.worldPosition, normal) : input.uv;
    const float4 albedo = baseColorTexture.Sample(baseColorSampler, uv) * baseColor;
    float3 light = ambientColor.rgb;
    for (uint i = 0; i < lightCount.x; ++i) {
        light += GetLight(lights[i], input.worldPosition, normal);
    }
    return float4(albedo.rgb * light, albedo.a);
}
