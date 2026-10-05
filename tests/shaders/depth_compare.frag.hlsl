cbuffer Reference : register(b0, space3) {
    float reference;
};

Texture2D<float> depth : register(t0, space2);
SamplerComparisonState depthSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0 {
    const float passed = depth.SampleCmpLevelZero(depthSampler, input.uv, reference);
    return float4(passed, passed, passed, 1.0);
}
