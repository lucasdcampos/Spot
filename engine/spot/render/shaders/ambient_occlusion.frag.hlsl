// Ambient occlusion from the camera's depth (scalable ambient obscurance, McGuire et al. 2012): how much of the light
// from all around a pixel's surface the surfaces within a radius of it in the world hide. Points on a spiral around
// the pixel, turned by a different angle at each pixel, are each compared with the plane of its surface, whose normal
// the depth gives too; AmbientOcclusion blurs the noise of the turns away after.
Texture2D<float> depth : register(t0, space2);
SamplerState depthSampler : register(s0, space2);

cbuffer Occlusion : register(b0, space3) {
    // Takes clip-space positions, at the depths the camera's depth holds, to view space.
    float4x4 inverseProjection;
    // The view's size in pixels, and its inverse.
    float4 viewSize;
    // The radius in world units, the intensity, the pixels a world unit covers at a distance of one unit (or at any
    // distance, for orthographic cameras), and 1 for orthographic cameras.
    float4 params;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float Pi = 3.14159265;
#define SAMPLES 12
// How many times the spiral turns around the pixel, prime to the samples, so they spread evenly in angle.
#define TURNS 7.0

float3 GetViewPosition(int2 pixel) {
    const int2 at = clamp(pixel, int2(0, 0), int2(viewSize.xy) - 1);
    const float stored = depth.Load(int3(at, 0));
    const float2 uv = (float2(at) + 0.5) * viewSize.zw;
    const float4 view = mul(inverseProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, stored, 1.0));
    return view.xyz / view.w;
}

float4 main(Input input) : SV_Target0 {
    const int2 pixel = int2(input.position.xy);
    if (depth.Load(int3(pixel, 0)) >= 1.0) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    const float3 position = GetViewPosition(pixel);
    // Along each axis, the neighbor on the same surface: the one nearer in depth, so edges keep the normal of the
    // surface the pixel is on.
    const float3 right = GetViewPosition(pixel + int2(1, 0)) - position;
    const float3 left = position - GetViewPosition(pixel - int2(1, 0));
    const float3 down = GetViewPosition(pixel + int2(0, 1)) - position;
    const float3 up = position - GetViewPosition(pixel - int2(0, 1));
    const float3 alongX = abs(right.z) < abs(left.z) ? right : left;
    const float3 alongY = abs(down.z) < abs(up.z) ? down : up;
    // Screen rows go down, so this faces the camera.
    const float3 normal = normalize(cross(alongY, alongX));

    const float radius = params.x;
    const float projected = params.w > 0.5 ? radius * params.z : radius * params.z / max(-position.z, 1e-3);
    if (projected < 1.0) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    // Interleaved gradient noise (Jimenez 2014), which a small blur evens out.
    const float spin = 2.0 * Pi * frac(52.9829189 * frac(dot(float2(pixel), float2(0.06711056, 0.00583715))));
    const float radius2 = radius * radius;
    const float bias = 0.03 * radius;
    float sum = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        const float along = (float(i) + 0.5) / SAMPLES;
        const float angle = along * TURNS * 2.0 * Pi + spin;
        const float2 offset = float2(cos(angle), sin(angle)) * along * projected;
        const float3 toSample = GetViewPosition(pixel + int2(round(offset))) - position;
        const float distance2 = dot(toSample, toSample);
        const float falloff = max(radius2 - distance2, 0.0);
        sum += falloff * falloff * falloff * max((dot(toSample, normal) - bias) / (distance2 + 0.01), 0.0);
    }
    const float occlusion = max(1.0 - sum * params.y * 5.0 / (radius2 * radius2 * radius2 * SAMPLES), 0.0);
    return float4(occlusion, occlusion, occlusion, 1.0);
}
