Texture2DArray<float4> source : register(t0, space2);
SamplerState sourceSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// mip_downsample.frag.hlsl for a face of a cube texture, which is read through a one-layer array view of the face.
float4 main(Input input) : SV_Target0 {
    return source.SampleLevel(sourceSampler, float3(input.uv, 0.0), 0.0);
}
