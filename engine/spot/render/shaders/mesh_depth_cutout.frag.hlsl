// Drops the pixels of a cutout material that mesh.frag.hlsl drops, read at the same UVs from the same vertex shader, so
// the depth prepass holds only what the view's pass then keeps; the rasterizer writes the depth of the others.
#include "mesh_input.hlsl"

Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState baseColorSampler : register(s0, space2);

cbuffer Cutout : register(b0, space3) {
    // The material's base color alpha, its cutoff and its worldTextureSize.
    float4 cutout;
};

void main(MeshFragmentInput input) {
    if (baseColorTexture.Sample(baseColorSampler, GetMeshUv(input, cutout.z)).a * cutout.x < cutout.y) {
        discard;
    }
}
