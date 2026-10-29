#include "clouds_volume.hlsl"

// Draws the clouds, marched at half the view's size, over the opaque surfaces of the whole view, blending them over
// what they hide. Each pixel takes the four nearest of their pixels, weighted by how near and by how alike the
// distance of the surface each was marched against is to its own, so clouds do not bleed across the edges of
// mountains and buildings in front of them.

Texture2D<float4> clouds : register(t0, space2);
SamplerState cloudsSampler : register(s0, space2);
Texture2D<float> sceneDepth : register(t1, space2);
SamplerState sceneDepthSampler : register(s1, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float GetDistanceAt(float2 uv) {
    const int2 pixel = min(int2(uv * viewSize.xy), int2(viewSize.xy) - 1);
    return GetSceneDistance(uv, sceneDepth.Load(int3(pixel, 0)));
}

float4 main(Input input) : SV_Target0 {
    const float distance = GetDistanceAt(input.uv);
    const float2 at = input.uv * cloudSize.xy - 0.5;
    const float2 corner = floor(at);
    const float2 f = at - corner;
    const int2 last = int2(cloudSize.xy) - 1;
    float4 sum = 0.0;
    float total = 0.0;
    for (int y = 0; y <= 1; ++y) {
        for (int x = 0; x <= 1; ++x) {
            const int2 pixel = clamp(int2(corner) + int2(x, y), int2(0, 0), last);
            const float near = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
            const float marched = GetDistanceAt((float2(pixel) + 0.5) * cloudSize.zw);
            const float unlike = abs(marched - distance) / max(min(marched, distance), 1.0);
            const float w = near / (1e-3 + unlike) + 1e-6;
            sum += clouds.Load(int3(pixel, 0)) * w;
            total += w;
        }
    }
    return sum / total;
}
