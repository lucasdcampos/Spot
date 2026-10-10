// A fullscreen pass's color, from its uniforms.
cbuffer Solid : register(b0, space3) {
    float4 color;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0 {
    return color;
}
