cbuffer Sky : register(b0, space1) {
    // Turns clip-space positions into directions from the camera; the camera's position plays no part, since the sky
    // is infinitely far away.
    float4x4 inverseViewProjection;
};

struct Output {
    float4 position : SV_Position;
    // Divided by w per pixel, since only the clip-space position interpolates linearly across the screen.
    float4 direction : TEXCOORD0;
};

// A single triangle that covers the whole view, built from the vertex index.
Output main(uint vertexId : SV_VertexID) {
    const float2 position = float2((vertexId << 1) & 2, vertexId & 2) * 2.0 - 1.0;
    Output output;
    // On the far plane, so it shows only where nothing drawn before it, such as the depth prepass, hides it.
    output.position = float4(position, 1.0, 1.0);
    output.direction = mul(inverseViewProjection, float4(position, 0.5, 1.0));
    return output;
}
