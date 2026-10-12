// Draws the depth of meshes into a tile of a shadow map, as the light sees them.
cbuffer View : register(b0, space1) {
    float4x4 viewProjection;
};

struct Instance {
    float4x4 model;
    float4x4 normalMatrix;
    uint4 skin;
};

// The meshes' transforms, as the mesh shader reads them, and the view's casters, as indices into them.
StructuredBuffer<Instance> instances : register(t0, space0);
StructuredBuffer<uint> casters : register(t1, space0);

StructuredBuffer<float4x4> palettes : register(t2, space0);

cbuffer Batch : register(b1, space1) {
    uint4 firstInstance;
};

struct Input {
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    uint4 joints : TEXCOORD2;
    float4 weights : TEXCOORD3;
};

struct Output {
    float4 position : SV_Position;
    // For cutout materials, whose dropped pixels cast no shadow.
    float2 uv : TEXCOORD0;
};

Output main(Input input, uint instanceId : SV_InstanceID) {
    const Instance instance = instances[casters[firstInstance.x + instanceId]];
    const float4x4 model = instance.model;
    const float4x4 skin = palettes[instance.skin.x + input.joints.x] * input.weights.x +
                          palettes[instance.skin.x + input.joints.y] * input.weights.y +
                          palettes[instance.skin.x + input.joints.z] * input.weights.z +
                          palettes[instance.skin.x + input.joints.w] * input.weights.w;
    input.position = mul(skin, float4(input.position, 1.0)).xyz;
    Output output;
    output.position = mul(viewProjection, mul(model, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}
