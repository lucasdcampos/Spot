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
};

struct Input {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 main(Input input) : SV_Target0 {
    const float4 albedo = baseColorTexture.Sample(baseColorSampler, input.uv) * baseColor;
    const float facing = dot(normalize(input.normal), -lightDirection.xyz);
    // Back faces of double-sided surfaces have normals pointing away from the viewer, so they are lit as if flipped.
    const float diffuse = doubleSided > 0.5 ? abs(facing) : max(facing, 0.0);
    return float4(albedo.rgb * (ambientColor.rgb + lightColor.rgb * diffuse), albedo.a);
}
