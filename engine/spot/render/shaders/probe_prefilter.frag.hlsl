// Blurs a reflection probe's captured cube map for surfaces of a roughness, into the probe's tile of a mip level in the
// reflections' atlas, which the pass's viewport covers: octahedral (see EncodeOctahedral()), with a border of one
// texel around the square that repeats its edge, so filtering never reads the tile next to it.
#include "environment.hlsl"

TextureCube<float4> environment : register(t0, space2);
SamplerState environmentSampler : register(s0, space2);

cbuffer Prefilter : register(b0, space3) {
    // The roughness, the size of the environment's faces, its number of mip levels, and the tile's size in texels.
    float4 params;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0 {
    const float size = params.w;
    const float2 square = saturate((input.uv * size - 1.0) / (size - 2.0));
    const float3 normal = DecodeOctahedral(square);
    return float4(PrefilterEnvironment(environment, environmentSampler, normal, params.x, params.y, params.z), 1.0);
}
