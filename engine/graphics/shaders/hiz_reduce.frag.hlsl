// Reduces depth to the farthest depth of each tile of it, which occlusion culling tests what is behind against.
cbuffer Reduce : register(b0, space3) {
    // x: the tile's size in texels; yz: the depth's size.
    uint4 settings;
};

Texture2D<float> depth : register(t0, space2);
SamplerState depthSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float main(Input input) : SV_Target0 {
    const uint2 first = uint2(input.position.xy) * settings.x;
    const uint2 end = min(first + settings.x, settings.yz);
    float farthest = 0.0;
    for (uint y = first.y; y < end.y; ++y) {
        for (uint x = first.x; x < end.x; ++x) {
            farthest = max(farthest, depth.Load(int3(x, y, 0)));
        }
    }
    return farthest;
}
