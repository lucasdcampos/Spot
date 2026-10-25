// Blurs the sky's cube map for surfaces of a roughness, into one face of a mip level of the reflections' cube map:
// each texel averages the sky over the GGX lobe of a mirror direction (see PrefilterEnvironment()).
#include "environment.hlsl"

TextureCube<float4> environment : register(t0, space2);
SamplerState environmentSampler : register(s0, space2);

cbuffer Prefilter : register(b0, space3) {
    // The face's directions, as TextureType::Cube lays them out.
    float4 forward;
    float4 right;
    float4 up;
    // The roughness, the size of the environment's faces, and its number of mip levels.
    float4 params;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0 {
    const float3 normal = normalize(forward.xyz + (input.uv.x * 2.0 - 1.0) * right.xyz + (1.0 - input.uv.y * 2.0) * up.xyz);
    return float4(PrefilterEnvironment(environment, environmentSampler, normal, params.x, params.y, params.z), 1.0);
}
