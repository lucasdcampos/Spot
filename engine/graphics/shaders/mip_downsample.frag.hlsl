Texture2D<float4> source : register(t0, space2);
SamplerState sourceSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// A level of a mipmap chain from the one above it, for backends whose API has no mipmap generation: the linear sample
// at the middle of each texel of the level averages the four texels of the larger one it covers. The source is a view
// of the level above; mip_downsample_layer.frag.hlsl reads a face of a cube instead.
float4 main(Input input) : SV_Target0 {
    return source.SampleLevel(sourceSampler, input.uv, 0.0);
}
