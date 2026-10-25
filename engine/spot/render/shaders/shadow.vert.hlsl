// Draws the depth of meshes into a tile of a shadow map, as the light sees them.
cbuffer View : register(b0, space1) {
    float4x4 viewProjection;
    // The planes of the view, which batches culled here test their instances' bounds against.
    float4 cullPlanes[6];
    // Where the camera that picks levels of detail is, and how it sizes things (see InSizeRange()).
    float4 lodView;
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

// x is the first of the batch's casters. In y, bit 0 reads instances directly and culls them here; bit 1 reads
// the compact visible list of an indirect camera-depth draw. Shadow passes use their own caster lists.
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
    float2 uv : TEXCOORD1;
};

struct Output {
    float4 position : SV_Position;
    // For cutout materials, whose dropped pixels cast no shadow.
    float2 uv : TEXCOORD0;
};

Output main(Input input, uint instanceId : SV_InstanceID) {
    const uint index = (firstInstance.y & 1) != 0 ? firstInstance.x + instanceId : casters[firstInstance.x + instanceId];
    const Instance instance = instances[index];
    Output output;
    if ((firstInstance.y & 1) != 0 && (!IsInView(instance.bounds) || !InSizeRange(instance.bounds, instance.skin))) {
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
