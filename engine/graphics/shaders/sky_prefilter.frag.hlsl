// Blurs the sky's cube map for surfaces of a roughness, into one face of a mip level of the reflections' cube map:
// each texel averages the sky over the GGX lobe of a mirror direction, as split-sum image-based lighting does, taking
// the view and the normal to be that direction.
TextureCube<float4> environment : register(t0, space2);
SamplerState environmentSampler : register(s0, space2);

cbuffer Prefilter : register(b0, space3) {
    // The face's directions, as TextureType::Cube lays them out.
    float4 forward;
    float4 right;
    float4 up;
    // The roughness, the size of the environment's faces, and its number of mip levels.
    float4 params;
};

struct Input {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float Pi = 3.14159265;
#define SAMPLES 64

// Points spread evenly over the unit square.
float2 Hammersley(uint i) {
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float2(float(i) / SAMPLES, float(bits) * 2.3283064365386963e-10);
}

float4 main(Input input) : SV_Target0 {
    const float3 normal = normalize(forward.xyz + (input.uv.x * 2.0 - 1.0) * right.xyz + (1.0 - input.uv.y * 2.0) * up.xyz);
    const float roughness = params.x;
    if (roughness <= 0.0) {
        return float4(environment.SampleLevel(environmentSampler, normal, 0.0).rgb, 1.0);
    }
    const float alpha = roughness * roughness;
    const float a2 = alpha * alpha;
    const float3 helper = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(helper, normal));
    const float3 bitangent = cross(normal, tangent);
    // Samples where the lobe is wide read blurrier levels of the environment, so few samples still cover it without
    // speckles: each takes the level whose texels are about the solid angle it stands for.
    const float texelSolidAngle = 4.0 * Pi / (6.0 * params.y * params.y);

    float3 sum = 0.0;
    float weight = 0.0;
    for (uint i = 0; i < SAMPLES; ++i) {
        const float2 xi = Hammersley(i);
        const float phi = 2.0 * Pi * xi.x;
        const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
        const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        const float3 halfway = tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + normal * cosTheta;
        const float3 toLight = 2.0 * dot(normal, halfway) * halfway - normal;
        const float nl = dot(normal, toLight);
        if (nl > 0.0) {
            const float d = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
            const float distribution = a2 / (Pi * d * d);
            // With the view along the normal, the pdf of the reflected direction is D / 4.
            const float sampleSolidAngle = 1.0 / (SAMPLES * distribution * 0.25 + 1e-4);
            const float level = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, params.z - 1.0);
            sum += environment.SampleLevel(environmentSampler, toLight, level).rgb * nl;
            weight += nl;
        }
    }
    return float4(sum / max(weight, 1e-4), 1.0);
}
