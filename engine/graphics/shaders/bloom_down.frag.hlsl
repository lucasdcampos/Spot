cbuffer BloomDown : register(b0, space3) {
    // The source's texel size, then the threshold and its soft knee, both 0 past the first level.
    float4 settings;
};

Texture2D<float4> source : register(t0, space2);
SamplerState sourceSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float3 Tap(float2 uv, float2 offset) {
    return source.Sample(sourceSampler, uv + offset * settings.xy).rgb;
}

// Halves the image with 13 taps (Jimenez, 2014), which keeps bright details from flickering as they move, and on the
// first level keeps only light above the threshold, fading in over the knee so there is no hard edge.
float4 main(Input input) : SV_Target0 {
    const float2 uv = input.uv;
    const float3 a = Tap(uv, float2(-2.0, -2.0));
    const float3 b = Tap(uv, float2(0.0, -2.0));
    const float3 c = Tap(uv, float2(2.0, -2.0));
    const float3 d = Tap(uv, float2(-2.0, 0.0));
    const float3 e = Tap(uv, float2(0.0, 0.0));
    const float3 f = Tap(uv, float2(2.0, 0.0));
    const float3 g = Tap(uv, float2(-2.0, 2.0));
    const float3 h = Tap(uv, float2(0.0, 2.0));
    const float3 i = Tap(uv, float2(2.0, 2.0));
    const float3 j = Tap(uv, float2(-1.0, -1.0));
    const float3 k = Tap(uv, float2(1.0, -1.0));
    const float3 l = Tap(uv, float2(-1.0, 1.0));
    const float3 m = Tap(uv, float2(1.0, 1.0));
    float3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;

    const float threshold = settings.z;
    const float knee = settings.w;
    if (threshold > 0.0 || knee > 0.0) {
        // A single blinding pixel, such as the sun's, would otherwise flood the whole image.
        color = min(color, 64.0);
        const float brightness = max(max(color.r, color.g), color.b);
        float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 1e-5);
        color *= max(soft, brightness - threshold) / max(brightness, 1e-5);
    }
    return float4(max(color, 0.0), 1.0);
}
