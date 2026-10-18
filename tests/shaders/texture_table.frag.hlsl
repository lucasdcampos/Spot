// Reads the first texel of the texture of a texture table its uniforms pick.
Texture2D<float4> textures[] : register(t0, space4);

cbuffer Pick : register(b0, space3) {
    uint index;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0 {
    return textures[index].Load(int3(0, 0, 0));
}
