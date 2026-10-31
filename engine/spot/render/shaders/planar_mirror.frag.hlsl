Texture2D<float4> reflection : register(t0, space2);
SamplerState reflectionSampler : register(s0, space2);
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    return float4(reflection.Sample(reflectionSampler, uv).rgb, 1);
}
