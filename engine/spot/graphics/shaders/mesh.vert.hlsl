cbuffer Frame : register(b0, space1) {
    float4x4 viewProjection;
    // The planes of the view, which batches culled here test their instances' bounds against.
    float4 cullPlanes[6];
    // Where the camera that picks levels of detail is, and how it sizes things (see InSizeRange()).
    float4 lodView;
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

// The instances of render worlds that the culling pass kept, batch by batch (see world_cull.comp.hlsl).
StructuredBuffer<uint> visible : register(t1, space0);

// x is the first instance of the batch; y has bit 0 set for batches whose instances are culled here, against the
// view, and bit 1 for those the culling pass kept the instances of, which are read through visible.
cbuffer Batch : register(b1, space1) {
    uint4 firstInstance;
};

// Whether the sphere may show in the view; the instances of a culled batch that do not are drawn as nothing.
// Whether the instance shows at its screen size: the level of detail it is of is in skin.zw, which world instances
// keep no skin in. The size is the radius times w over the distance, or times -w for orthographic cameras.
bool InSizeRange(float4 bounds, uint4 skin) {
    const float distance = length(bounds.xyz - lodView.xyz);
    const float size = lodView.w < 0.0 ? bounds.w * -lodView.w
                                       : (distance <= bounds.w ? 1.0 : bounds.w * lodView.w / distance);
    return size >= asfloat(skin.z) && size < asfloat(skin.w);
}

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
    const uint index = (firstInstance.y & 2) != 0 ? visible[firstInstance.x + instanceId] : firstInstance.x + instanceId;
    const Instance instance = instances[index];
    const float4x4 model = instance.model;
    const float4x4 normalMatrix = instance.normalMatrix;
    Output output;
    if ((firstInstance.y & 1) != 0 && (!IsInView(instance.bounds) || !InSizeRange(instance.bounds, instance.skin))) {
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
