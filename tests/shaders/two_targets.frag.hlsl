// Writes a different color into each of two targets at once.
struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct Output {
    float4 first : SV_Target0;
    float4 second : SV_Target1;
};

Output main(Input input) {
    Output output;
    output.first = float4(1.0, 0.0, 0.0, 1.0);
    output.second = float4(0.75, 0.0, 0.0, 1.0);
    return output;
}
