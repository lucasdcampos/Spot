// A 3D texture's slices, one per row of the target from its top: the texture coordinate down the target is its depth.
Texture3D<float4> volume : register(t0, space2);
SamplerState volumeSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0 {
    return volume.Sample(volumeSampler, float3(0.5, 0.5, input.uv.y));
}
