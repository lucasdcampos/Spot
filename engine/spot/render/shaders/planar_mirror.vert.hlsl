cbuffer Mirror : register(b0, space1) {
    float4x4 viewProjection;
    float4x4 world;
};
struct Output {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
Output main(uint id : SV_VertexID) {
    const float2 corners[6] = {float2(0,0), float2(1,0), float2(0,1), float2(0,1), float2(1,0), float2(1,1)};
    float2 uv = corners[id];
    Output output;
    output.position = mul(viewProjection, mul(world, float4(uv - 0.5, 0, 1)));
    output.uv = float2(1 - uv.x, 1 - uv.y);
    return output;
}
