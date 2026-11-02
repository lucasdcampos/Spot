struct Output {
    float4 position : SV_Position;
    // From (0, 0) at the target's top-left corner to (1, 1) at its bottom-right one.
    float2 uv : TEXCOORD0;
};

// A single triangle that covers the whole target, built from the vertex index: the corners of the target are at
// half of its sides.
Output main(uint vertexId : SV_VertexID) {
    const float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    Output output;
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    output.uv = float2(corner.x, 1.0 - corner.y);
    return output;
}
