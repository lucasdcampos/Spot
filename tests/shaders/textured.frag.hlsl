// Declares one resource of each kind the reflection should count.
cbuffer Tint : register(b0, space3) {
    float4 tint;
};

Texture2D<float4> image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    return image.Sample(imageSampler, uv) * tint;
}
