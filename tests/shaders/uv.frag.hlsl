// A fullscreen pass's texture coordinates, as red and green.
struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0 {
    return float4(input.uv, 0.0, 1.0);
}
