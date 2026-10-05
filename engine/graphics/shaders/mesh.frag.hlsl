Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState baseColorSampler : register(s0, space2);

cbuffer Lighting : register(b0, space3) {
    // The direction the light travels in, normalized.
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
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

float4 main(Input input) : SV_Target0 {
    const float3 normal = normalize(input.normal);
    const float2 uv = worldTextureSize > 0.0 ? GetWorldUv(input.worldPosition, normal) : input.uv;
    const float4 albedo = baseColorTexture.Sample(baseColorSampler, uv) * baseColor;
    const float facing = dot(normal, -lightDirection.xyz);
    // Back faces of double-sided surfaces have normals pointing away from the viewer, so they are lit as if flipped.
    const float diffuse = doubleSided > 0.5 ? abs(facing) : max(facing, 0.0);
    return float4(albedo.rgb * (ambientColor.rgb + lightColor.rgb * diffuse), albedo.a);
}
