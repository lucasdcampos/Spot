// Draws the depth of meshes into a tile of a shadow map, as the light sees them.
cbuffer View : register(b0, space1) {
    float4x4 viewProjection;
};

cbuffer Object : register(b1, space1) {
    float4x4 model;
};

struct Input {
    float3 position : TEXCOORD0;
};

float4 main(Input input) : SV_Position {
    return mul(viewProjection, mul(model, float4(input.position, 1.0)));
}
