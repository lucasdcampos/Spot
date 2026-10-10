cbuffer Fxaa : register(b0, space3) {
    // The image's texel size.
    float4 settings;
};

Texture2D<float4> image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float ReduceMultiplier = 1.0 / 8.0;
static const float ReduceMinimum = 1.0 / 128.0;
// In texels: how far along an edge it looks.
static const float SpanMaximum = 8.0;

// Brightness as the eye sees it, on roughly perceptual values, where edges are judged.
float Luma(float3 color) {
    return dot(sqrt(saturate(color)), float3(0.299, 0.587, 0.114));
}

float3 Tap(float2 uv) {
    return image.SampleLevel(imageSampler, uv, 0.0).rgb;
}

// Fast approximate antialiasing (Timothy Lottes): finds where the brightness changes sharply and in which direction
// the edge runs, then blends along it, smoothing the steps of edges, and of thin highlights, that multisampling does
// not reach.
float4 main(Input input) : SV_Target0 {
    const float2 texel = settings.xy;
    const float2 uv = input.uv;
    const float4 center = image.SampleLevel(imageSampler, uv, 0.0);
    const float lumaNorthWest = Luma(Tap(uv + float2(-1.0, -1.0) * texel));
    const float lumaNorthEast = Luma(Tap(uv + float2(1.0, -1.0) * texel));
    const float lumaSouthWest = Luma(Tap(uv + float2(-1.0, 1.0) * texel));
    const float lumaSouthEast = Luma(Tap(uv + float2(1.0, 1.0) * texel));
    const float lumaCenter = Luma(center.rgb);
    const float lumaMin = min(lumaCenter, min(min(lumaNorthWest, lumaNorthEast), min(lumaSouthWest, lumaSouthEast)));
    const float lumaMax = max(lumaCenter, max(max(lumaNorthWest, lumaNorthEast), max(lumaSouthWest, lumaSouthEast)));

    float2 direction = float2(-((lumaNorthWest + lumaNorthEast) - (lumaSouthWest + lumaSouthEast)),
                              (lumaNorthWest + lumaSouthWest) - (lumaNorthEast + lumaSouthEast));
    const float reduce = max((lumaNorthWest + lumaNorthEast + lumaSouthWest + lumaSouthEast) * 0.25 * ReduceMultiplier,
                             ReduceMinimum);
    const float scale = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * scale, -SpanMaximum, SpanMaximum) * texel;

    const float3 near = 0.5 * (Tap(uv + direction * (1.0 / 3.0 - 0.5)) + Tap(uv + direction * (2.0 / 3.0 - 0.5)));
    const float3 far = near * 0.5 + 0.25 * (Tap(uv + direction * -0.5) + Tap(uv + direction * 0.5));
    // Reaching too far crosses another edge, whose colors do not belong here.
    const float lumaFar = Luma(far);
    const float3 color = lumaFar < lumaMin || lumaFar > lumaMax ? near : far;
    return float4(color, center.a);
}
