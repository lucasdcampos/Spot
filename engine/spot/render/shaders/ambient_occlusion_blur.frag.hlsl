// Blurs ambient occlusion along one axis, across pixels at about the same depth only, so it smooths the noise of the
// spiral without bleeding across edges.
Texture2D<float> occlusion : register(t0, space2);
SamplerState occlusionSampler : register(s0, space2);
Texture2D<float> depth : register(t1, space2);
SamplerState depthSampler : register(s1, space2);

cbuffer Blur : register(b0, space3) {
    // The step between taps in pixels, and the view's size in pixels.
    float4 stepAndSize;
    // The camera's near and far planes, and 1 for orthographic cameras.
    float4 planes;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// A Gaussian over four taps on each side.
static const float Weights[5] = {0.153170, 0.144893, 0.122649, 0.092902, 0.062970};

float GetDistance(float stored) {
    return planes.z > 0.5 ? lerp(planes.x, planes.y, stored)
                          : planes.x * planes.y / (planes.y - stored * (planes.y - planes.x));
}

float4 main(Input input) : SV_Target0 {
    const int2 pixel = int2(input.position.xy);
    const float stored = depth.Load(int3(pixel, 0));
    if (stored >= 1.0) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    const float distance = GetDistance(stored);
    const int2 last = int2(stepAndSize.zw) - 1;
    float sum = occlusion.Load(int3(pixel, 0)) * Weights[0];
    float weight = Weights[0];
    [unroll] for (int i = 1; i < 5; ++i) {
        [unroll] for (int side = -1; side <= 1; side += 2) {
            const int2 at = clamp(pixel + int2(stepAndSize.xy) * (i * side), int2(0, 0), last);
            const float tapDistance = GetDistance(depth.Load(int3(at, 0)));
            const float tapWeight = Weights[i] * max(1.0 - 8.0 * abs(tapDistance - distance) / distance, 0.0);
            sum += occlusion.Load(int3(at, 0)) * tapWeight;
            weight += tapWeight;
        }
    }
    const float blurred = sum / weight;
    return float4(blurred, blurred, blurred, 1.0);
}
