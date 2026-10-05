// Builds a triangle from the vertex index, so it needs no vertex buffer.
struct Output {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

Output main(uint vertexId : SV_VertexID) {
    const float2 positions[3] = {float2(0.0, 0.5), float2(-0.5, -0.5), float2(0.5, -0.5)};
    const float3 colors[3] = {float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0)};

    Output output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.color = float4(colors[vertexId], 1.0);
    return output;
}
