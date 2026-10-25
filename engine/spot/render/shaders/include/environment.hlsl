// What Renderer3D's light from all around is made with: GGX blurring of a cube map for a roughness, as split-sum
// image-based lighting does, and the octahedral mapping that lays the directions around a point out on a square, which
// reflection probes are kept in.
#ifndef SPOT_ENVIRONMENT_HLSL
#define SPOT_ENVIRONMENT_HLSL

static const float EnvironmentPi = 3.14159265;
#define ENVIRONMENT_SAMPLES 64

// Points spread evenly over the unit square.
float2 Hammersley(uint i) {
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float2(float(i) / ENVIRONMENT_SAMPLES, float(bits) * 2.3283064365386963e-10);
}

// The environment averaged over the GGX lobe of the direction, taking the view and the normal to be that direction.
// faceSize is the size of the environment's faces, and levels its number of mip levels.
float3 PrefilterEnvironment(TextureCube<float4> environment, SamplerState environmentSampler, float3 normal,
                            float roughness, float faceSize, float levels) {
    if (roughness <= 0.0) {
        return environment.SampleLevel(environmentSampler, normal, 0.0).rgb;
    }
    const float alpha = roughness * roughness;
    const float a2 = alpha * alpha;
    const float3 helper = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(helper, normal));
    const float3 bitangent = cross(normal, tangent);
    // Samples where the lobe is wide read blurrier levels of the environment, so few samples still cover it without
    // speckles: each takes the level whose texels are about the solid angle it stands for.
    const float texelSolidAngle = 4.0 * EnvironmentPi / (6.0 * faceSize * faceSize);

    float3 sum = 0.0;
    float weight = 0.0;
    for (uint i = 0; i < ENVIRONMENT_SAMPLES; ++i) {
        const float2 xi = Hammersley(i);
        const float phi = 2.0 * EnvironmentPi * xi.x;
        const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
        const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        const float3 halfway = tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + normal * cosTheta;
        const float3 toLight = 2.0 * dot(normal, halfway) * halfway - normal;
        const float nl = dot(normal, toLight);
        if (nl > 0.0) {
            const float d = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
            const float distribution = a2 / (EnvironmentPi * d * d);
            // With the view along the normal, the pdf of the reflected direction is D / 4.
            const float sampleSolidAngle = 1.0 / (ENVIRONMENT_SAMPLES * distribution * 0.25 + 1e-4);
            const float level = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, levels - 1.0);
            sum += environment.SampleLevel(environmentSampler, toLight, level).rgb * nl;
            weight += nl;
        }
    }
    return sum / max(weight, 1e-4);
}

// 1 or -1, never 0, by the sign of each component.
float2 SignNotZero(float2 v) {
    return float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

// Where a direction is on the octahedral square, from (0, 0) to (1, 1): the upper half (+Y) fills its middle, a
// diamond, and the lower half folds into its corners, so neighboring directions stay neighbors but along its edges.
float2 EncodeOctahedral(float3 direction) {
    const float3 d = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
    float2 square = d.xz;
    if (d.y < 0.0) {
        square = (1.0 - abs(d.zx)) * SignNotZero(d.xz);
    }
    return square * 0.5 + 0.5;
}

float3 DecodeOctahedral(float2 uv) {
    const float2 f = uv * 2.0 - 1.0;
    float3 d = float3(f.x, 1.0 - abs(f.x) - abs(f.y), f.y);
    const float fold = saturate(-d.y);
    d.x += d.x >= 0.0 ? -fold : fold;
    d.z += d.z >= 0.0 ? -fold : fold;
    return normalize(d);
}

#endif
