// Draws the depth of meshes into a tile of a shadow map, as the light sees them.
cbuffer View : register(b0, space1) {
    float4x4 viewProjection;
    // The planes of the view, which batches culled here test their instances' bounds against.
    float4 cullPlanes[6];
};

struct Instance {
    float4x4 model;
    float4x4 normalMatrix;
    uint4 skin;
    // Around the mesh in world space: center and radius.
    float4 bounds;
};

// The meshes' transforms, as the mesh shader reads them, and the view's casters, as indices into them.
StructuredBuffer<Instance> instances : register(t0, space0);
StructuredBuffer<uint> casters : register(t1, space0);

// x is the first of the batch's casters; y is set for batches whose instances are culled here, against the view,
// and read straight from the instances, from x on, rather than through the casters.
cbuffer Batch : register(b1, space1) {
    uint4 firstInstance;
};

// Whether the sphere may show in the view; the instances of a culled batch that do not are drawn as nothing.
bool IsInView(float4 bounds) {
    [unroll] for (int i = 0; i < 6; ++i) {
        if (dot(cullPlanes[i].xyz, bounds.xyz) + cullPlanes[i].w < -bounds.w) {
            return false;
        }
    }
    return true;
}

struct Input {
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

struct Output {
    float4 position : SV_Position;
    // For cutout materials, whose dropped pixels cast no shadow.
    float2 uv : TEXCOORD0;
};

Output main(Input input, uint instanceId : SV_InstanceID) {
    const uint index = firstInstance.y != 0 ? firstInstance.x + instanceId : casters[firstInstance.x + instanceId];
    const Instance instance = instances[index];
    Output output;
    if (firstInstance.y != 0 && !IsInView(instance.bounds)) {
        // Every vertex at the same point, so its triangles cover nothing.
        output.position = float4(2.0, 2.0, 2.0, 1.0);
        output.uv = float2(0.0, 0.0);
        return output;
    }
    const float4x4 model = instance.model;
    output.position = mul(viewProjection, mul(model, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}
