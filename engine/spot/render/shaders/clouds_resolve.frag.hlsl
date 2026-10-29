#include "clouds_volume.hlsl"

// Blends the clouds marched this frame with what the frames before gave the same clouds, which evens out the noise of
// rays that each skip most of the volume. Where the clouds a pixel sees were a frame ago comes from where its ray
// enters them, moved with the camera and the wind; what is found there is kept within the colors around the pixel now,
// so what is no longer there, such as clouds a mountain now hides, does not linger.

Texture2D<float4> current : register(t0, space2);
SamplerState currentSampler : register(s0, space2);
Texture2D<float4> history : register(t1, space2);
SamplerState historySampler : register(s1, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// How much of each frame's march goes into what is shown.
static const float NewShare = 0.1;

float4 main(Input input) : SV_Target0 {
    const int2 pixel = int2(input.position.xy);
    const int2 last = int2(cloudSize.xy) - 1;
    const float4 now = current.Load(int3(pixel, 0));
    if (reprojection.w < 0.5) {
        return now;
    }
    float4 lowest = now;
    float4 highest = now;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const float4 around = current.Load(int3(clamp(pixel + int2(x, y), int2(0, 0), last), 0));
            lowest = min(lowest, around);
            highest = max(highest, around);
        }
    }

    const float3 direction = GetRayDirection(input.uv);
    float2 segment;
    if (!GetCloudSegment(layer.x, layer.y, layer.z, direction.y, segment)) {
        return now;
    }
    // Inside the layer, what is seen is near, but not at the camera.
    const float distance = segment.x > 0.0 ? segment.x : min(segment.y, 1000.0) * 0.5;
    const float4 clip = mul(previousViewProjection, float4(direction * distance + reprojection.xyz, 1.0));
    if (clip.w <= 0.0) {
        return now;
    }
    const float2 uv = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0)) {
        return now;
    }
    const float4 before = clamp(history.SampleLevel(historySampler, uv, 0.0), lowest, highest);
    return lerp(before, now, NewShare);
}
