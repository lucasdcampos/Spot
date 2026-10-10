// Projects the light a surface receives from the sky's cube map onto the nine second-order spherical harmonics, one
// coefficient per pixel of a 9x1 target. Surfaces evaluate them by their normal for the sky's diffuse light: the
// coefficients are already convolved with the cosine lobe, and divided by pi, as the environment holds light in the
// units surfaces show it in.
TextureCube<float4> environment : register(t0, space2);
SamplerState environmentSampler : register(s0, space2);

cbuffer Irradiance : register(b0, space3) {
    // The size of the environment's faces, and the mip level whose faces are SAMPLES texels across.
    float4 params;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float Pi = 3.14159265;
#define SAMPLES 16

// The faces' directions, as TextureType::Cube lays them out.
static const float3 Forward[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0),
                                  float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
static const float3 Right[6] = {float3(0, 0, -1), float3(0, 0, 1), float3(1, 0, 0),
                                float3(1, 0, 0), float3(1, 0, 0), float3(-1, 0, 0)};
static const float3 Up[6] = {float3(0, 1, 0), float3(0, 1, 0), float3(0, 0, -1),
                             float3(0, 0, 1), float3(0, 1, 0), float3(0, 1, 0)};

float GetHarmonic(uint index, float3 d) {
    switch (index) {
    case 0:
        return 0.282095;
    case 1:
        return 0.488603 * d.y;
    case 2:
        return 0.488603 * d.z;
    case 3:
        return 0.488603 * d.x;
    case 4:
        return 1.092548 * d.x * d.y;
    case 5:
        return 1.092548 * d.y * d.z;
    case 6:
        return 0.315392 * (3.0 * d.z * d.z - 1.0);
    case 7:
        return 1.092548 * d.x * d.z;
    default:
        return 0.546274 * (d.x * d.x - d.y * d.y);
    }
}

float4 main(Input input) : SV_Target0 {
    const uint index = min((uint)input.position.x, 8u);
    float3 sum = 0.0;
    for (uint face = 0; face < 6; ++face) {
        for (uint y = 0; y < SAMPLES; ++y) {
            for (uint x = 0; x < SAMPLES; ++x) {
                const float u = (x + 0.5) / SAMPLES * 2.0 - 1.0;
                const float v = 1.0 - (y + 0.5) / SAMPLES * 2.0;
                const float3 direction = Forward[face] + u * Right[face] + v * Up[face];
                // The solid angle of the texel, smaller toward the face's corners.
                const float lengthSquared = dot(direction, direction);
                const float solidAngle = 4.0 / (SAMPLES * SAMPLES * lengthSquared * sqrt(lengthSquared));
                const float3 normalized = direction * rsqrt(lengthSquared);
                sum += environment.SampleLevel(environmentSampler, normalized, params.y).rgb *
                       GetHarmonic(index, normalized) * solidAngle;
            }
        }
    }
    // The cosine lobe's convolution scales each band: pi, 2pi/3 and pi/4.
    const float band = index == 0 ? Pi : index < 4 ? 2.0 * Pi / 3.0 : Pi / 4.0;
    return float4(sum * band / Pi, 1.0);
}
