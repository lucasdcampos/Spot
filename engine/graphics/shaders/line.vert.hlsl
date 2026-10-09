cbuffer Camera : register(b0, space1) {
    float4x4 viewProjection;
};

struct Input {
    float3 position : TEXCOORD0;
    float4 color : TEXCOORD1;
};

struct Output {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

Output main(Input input) {
    Output output;
    output.position = mul(viewProjection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}
