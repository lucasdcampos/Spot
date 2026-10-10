cbuffer Tonemap : register(b0, space3) {
    // The exposure, the operator (0 ACES, 1 AgX, 2 linear) and whether to dither.
    float4 settings;
};

Texture2D<float4> hdr : register(t0, space2);
SamplerState hdrSampler : register(s0, space2);

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// ACES's reference rendering and output transforms, fitted by Stephen Hill: in and out of the ACES working space,
// around a curve of both.
float3 Aces(float3 color) {
    static const float3x3 ToAces = {
        {0.59719, 0.35458, 0.04823},
        {0.07600, 0.90834, 0.01566},
        {0.02840, 0.13383, 0.83777},
    };
    static const float3x3 FromAces = {
        {1.60475, -0.53108, -0.07367},
        {-0.10208, 1.10813, -0.00605},
        {-0.00327, -0.07276, 1.07602},
    };
    color = mul(ToAces, color);
    const float3 a = color * (color + 0.0245786) - 0.000090537;
    const float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return saturate(mul(FromAces, a / b));
}

// Troy Sobotka's AgX, as Benjamin Wrensch's minimal fit of it: colors are taken into a wider space whose log encoding
// a sigmoid compresses, and back, so bright saturated light turns toward white as film does instead of clipping.
float3 AgX(float3 color) {
    // Written as GLSL builds its matrices, by columns, so vectors multiply from the left.
    static const float3x3 Inset = {
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104,
    };
    static const float3x3 Outset = {
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116,
    };
    static const float MinEv = -12.47393;
    static const float MaxEv = 4.026069;
    color = mul(max(color, 1e-10), Inset);
    color = (clamp(log2(color), MinEv, MaxEv) - MinEv) / (MaxEv - MinEv);
    const float3 x2 = color * color;
    const float3 x4 = x2 * x2;
    color = 15.5 * x4 * x2 - 40.14 * x4 * color + 31.96 * x4 - 6.868 * x2 * color + 0.4298 * x2 + 0.1191 * color -
            0.00232;
    color = mul(color, Outset);
    return pow(saturate(color), 2.2);
}

float4 main(Input input) : SV_Target0 {
    const float4 light = hdr.Sample(hdrSampler, input.uv);
    const float3 exposed = max(light.rgb, 0.0) * settings.x;
    float3 color = settings.y < 0.5 ? Aces(exposed) : settings.y < 1.5 ? AgX(exposed) : saturate(exposed);
    if (settings.z > 0.5) {
        // Noise of under a step of an 8-bit display hides the banding of smooth gradients, such as the sky's. Those
        // steps are even in sRGB, not in linear light, so the noise is added there.
        const float noise = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
        color = pow(max(pow(color, 1.0 / 2.2) + (noise - 0.5) / 255.0, 0.0), 2.2);
    }
    return float4(color, saturate(light.a));
}
