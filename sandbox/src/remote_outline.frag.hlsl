// Draws a line around the selection, from a mask of where the selected meshes cover the view.
Texture2D<float4> mask : register(t0, space2);
SamplerState maskSampler : register(s0, space2);

cbuffer Settings : register(b0, space3) {
    float4 color;
    // The size of one of the mask's pixels, in texture coordinates.
    float2 texelSize;
    // How far the line reaches out from the selection, in pixels, up to MaxReach.
    float width;
};

struct Input {
    float4 position : SV_Position;
    float2 clip : TEXCOORD0;
};

static const int MaxReach = 3;

float4 main(Input input) : SV_Target0 {
    const float2 uv = input.position.xy * texelSize;
    if (mask.Sample(maskSampler, uv).r > 0.5) {
        discard;
    }
    // The distance to the nearest selected pixel within reach; the line fades out at its edge, which smooths it.
    float nearest = MaxReach + 1.0;
    for (int y = -MaxReach; y <= MaxReach; ++y) {
        for (int x = -MaxReach; x <= MaxReach; ++x) {
            if (mask.Sample(maskSampler, uv + float2(x, y) * texelSize).r > 0.5) {
                nearest = min(nearest, length(float2(x, y)));
            }
        }
    }
    const float coverage = saturate(width + 0.5 - nearest);
    if (coverage <= 0.0) {
        discard;
    }
    return float4(color.rgb, color.a * coverage);
}
