Texture2D<float4> image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);

cbuffer TextMode : register(b0, space3) {
    float4 textMode;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

float4 main(Input input) : SV_Target0 {
    float4 sampleColor = image.Sample(imageSampler, input.uv);
    if (textMode.x > 0.5) {
        float width = max(fwidth(sampleColor.a), 1.0 / 255.0);
        sampleColor.a = smoothstep(128.0 / 255.0 - width * 0.5, 128.0 / 255.0 + width * 0.5, sampleColor.a);
        sampleColor.rgb = 1.0;
    }
    return sampleColor * input.color;
}
