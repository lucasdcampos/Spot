Texture2D<float4> image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);

cbuffer Settings : register(b0, space3) {
    // ImGui blends colors as they are displayed, but sRGB textures, such as rendered scenes, are sampled as linear
    // colors, so those are encoded back before blending.
    float encodeSrgb;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

float3 LinearToSrgb(float3 color) {
    const float3 low = color * 12.92;
    const float3 high = 1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055;
    return lerp(high, low, (float3)(color <= 0.0031308));
}

float4 main(Input input) : SV_Target0 {
    float4 texel = image.Sample(imageSampler, input.uv);
    if (encodeSrgb > 0.5) {
        texel.rgb = LinearToSrgb(texel.rgb);
    }
    return input.color * texel;
}
