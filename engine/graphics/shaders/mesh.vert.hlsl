cbuffer Frame : register(b0, space1) {
    float4x4 viewProjection;
};

struct Instance {
    float4x4 model;
    // The inverse transpose of the model matrix, which keeps normals perpendicular under non-uniform scale.
    float4x4 normalMatrix;
};

// Every mesh drawn by the frame's passes; an instanced draw reads its instances from the first of its batch on.
StructuredBuffer<Instance> instances : register(t0, space0);

cbuffer Batch : register(b1, space1) {
    uint4 firstInstance;
};

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
    const float4 worldPosition = mul(model, float4(input.position, 1.0));
    output.position = mul(viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = mul(normalMatrix, float4(input.normal, 0.0)).xyz;
    output.uv = input.uv;
    // Tangents follow the surface, so the model matrix itself turns them.
    output.tangent = float4(mul(model, float4(input.tangent.xyz, 0.0)).xyz, input.tangent.w);
    return output;
}
