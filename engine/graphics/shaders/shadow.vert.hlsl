// Draws the depth of meshes into a tile of a shadow map, as the light sees them.
cbuffer View : register(b0, space1) {
    float4x4 viewProjection;
};

struct Instance {
    float4x4 model;
    float4x4 normalMatrix;
};

// The meshes' transforms, as the mesh shader reads them.
StructuredBuffer<Instance> instances : register(t0, space0);

cbuffer Batch : register(b1, space1) {
    uint4 firstInstance;
};

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
    const float4x4 model = instances[firstInstance.x + instanceId].model;
    Output output;
    output.position = mul(viewProjection, mul(model, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}
