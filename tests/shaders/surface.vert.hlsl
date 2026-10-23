// A custom surface's vertices, already in world space, drawn through the view Renderer3D set for its pass.
#include "surface_frame.hlsl"

struct Input {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

struct Output {
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

Output main(Input input) {
    Output output;
    output.position = mul(viewProjection, float4(input.position, 1.0));
    output.worldPosition = input.position;
    output.normal = input.normal;
    return output;
}
