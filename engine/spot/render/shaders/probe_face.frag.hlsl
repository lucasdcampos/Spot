// Copies a face of a reflection probe, as a camera drew it, into its face of a cube map. Cameras keep right-handed
// axes, so right is forward crossed with up; the faces of cube maps have it the other way, so the copy mirrors the
// image left to right.
Texture2D<float4> face : register(t0, space2);
SamplerState faceSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0 {
    return float4(face.SampleLevel(faceSampler, float2(1.0 - input.uv.x, input.uv.y), 0.0).rgb, 1.0);
}
