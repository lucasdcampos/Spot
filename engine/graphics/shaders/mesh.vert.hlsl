cbuffer Frame : register(b0, space1) {
    float4x4 viewProjection;
    // The planes of the view, which batches culled here test their instances' bounds against.
    float4 cullPlanes[6];
};

struct Instance {
    float4x4 model;
    // The inverse transpose of the model matrix, which keeps normals perpendicular under non-uniform scale; its last
    // element is -1 for transforms that mirror.
    float4x4 normalMatrix;
    uint4 skin;
    // Around the mesh in world space: center and radius.
    float4 bounds;
};

// Every mesh drawn by the frame's passes; an instanced draw reads its instances from the first of its batch on.
StructuredBuffer<Instance> instances : register(t0, space0);

// x is the first instance of the batch; y is set for batches whose instances are culled here, against the view.
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
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
};

struct Output {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    // In world space, with the side the bitangent is on in w.
    float4 tangent : TEXCOORD3;
};

Output main(Input input, uint instanceId : SV_InstanceID) {
    // SV_InstanceID starts at 0 for each draw on every backend, whatever its first instance.
    const Instance instance = instances[firstInstance.x + instanceId];
    const float4x4 model = instance.model;
    const float4x4 normalMatrix = instance.normalMatrix;
    Output output;
    if (firstInstance.y != 0 && !IsInView(instance.bounds)) {
        // Every vertex at the same point, so its triangles cover nothing.
        output = (Output)0;
        output.position = float4(2.0, 2.0, 2.0, 1.0);
        return output;
    }
    const float4 worldPosition = mul(model, float4(input.position, 1.0));
    output.position = mul(viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = mul(normalMatrix, float4(input.normal, 0.0)).xyz;
    output.uv = input.uv;
    // Tangents follow the surface, so the model matrix itself turns them.
    output.tangent = float4(mul(model, float4(input.tangent.xyz, 0.0)).xyz, input.tangent.w * normalMatrix[3][3]);
    return output;
}
