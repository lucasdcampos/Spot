struct Output {
    float4 position : SV_Position;
    float2 clip : TEXCOORD0;
};

// A single triangle that covers the whole view, built from the vertex index.
Output main(uint vertexId : SV_VertexID) {
    const float2 position = float2((vertexId << 1) & 2, vertexId & 2) * 2.0 - 1.0;
    Output output;
    output.position = float4(position, 0.5, 1.0);
    output.clip = position;
    return output;
}
