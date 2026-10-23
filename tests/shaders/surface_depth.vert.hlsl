// A custom surface's vertices, in world space, through a view of its own: a light's, or the camera's for its depth.
cbuffer View : register(b0, space1) {
    float4x4 viewProjection;
};

struct Input {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

float4 main(Input input) : SV_Position {
    return mul(viewProjection, float4(input.position, 1.0));
}
