cbuffer Transform : register(b0, space1) {
    float4x4 transform;
};

struct Input {
    float2 position : TEXCOORD0;
    float4 color : TEXCOORD1;
};

struct Output {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

Output main(Input input) {
    Output output;
    output.position = mul(transform, float4(input.position, 0.0, 1.0));
    output.color = input.color;
    return output;
}
