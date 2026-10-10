// Draws the depth of meshes into a tile of a shadow map, as the light sees them.
cbuffer View : register(b0, space1) {
    float4x4 viewProjection;
};

cbuffer Object : register(b1, space1) {
    float4x4 model;
};

struct Input {
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

struct Output {
    float4 position : SV_Position;
    // For cutout materials, whose dropped pixels cast no shadow.
    float2 uv : TEXCOORD0;
};

Output main(Input input) {
    Output output;
    output.position = mul(viewProjection, mul(model, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}
