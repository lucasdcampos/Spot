// Drops the pixels of cutout materials whose alpha is below their cutoff, so the holes they leave cast no shadow;
// the rasterizer writes the depth of the others.
Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState baseColorSampler : register(s0, space2);

cbuffer Cutout : register(b0, space3) {
    // The material's base color alpha, and its cutoff.
    float4 alphaAndCutoff;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

void main(Input input) {
    if (baseColorTexture.Sample(baseColorSampler, input.uv).a * alphaAndCutoff.x < alphaAndCutoff.y) {
        discard;
    }
}
