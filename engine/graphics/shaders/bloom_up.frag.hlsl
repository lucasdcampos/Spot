cbuffer BloomUp : register(b0, space3) {
    // The smaller level's texel size, then how much of it each level takes over its own.
    float4 settings;
};

// The level of the downsampled chain, and the upsampled level below it, half its size.
Texture2D<float4> level : register(t0, space2);
SamplerState levelSampler : register(s0, space2);
Texture2D<float4> below : register(t1, space2);
SamplerState belowSampler : register(s1, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float3 Tap(float2 uv, float2 offset) {
    return below.Sample(belowSampler, uv + offset * settings.xy).rgb;
}

// Spreads the level below with a 3x3 tent and blends it over this one, so each level adds a wider, softer glow.
float4 main(Input input) : SV_Target0 {
    const float2 uv = input.uv;
    const float3 blurred = (Tap(uv, float2(-1.0, -1.0)) + Tap(uv, float2(1.0, -1.0)) + Tap(uv, float2(-1.0, 1.0)) +
                            Tap(uv, float2(1.0, 1.0))) * 0.0625 +
                           (Tap(uv, float2(0.0, -1.0)) + Tap(uv, float2(-1.0, 0.0)) + Tap(uv, float2(1.0, 0.0)) +
                            Tap(uv, float2(0.0, 1.0))) * 0.125 +
                           Tap(uv, float2(0.0, 0.0)) * 0.25;
    const float3 own = level.Sample(levelSampler, uv).rgb;
    return float4(lerp(own, blurred, settings.z), 1.0);
}
