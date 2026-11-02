// Places a mesh's triangles in the view, for drawing a flat color where it covers: which entity is under the mouse,
// or where the selection is.
cbuffer Object : register(b0, space1) {
    float4x4 modelViewProjection;
};

struct Input {
    float3 position : TEXCOORD0;
};

float4 main(Input input) : SV_Position {
    return mul(modelViewProjection, float4(input.position, 1.0));
}
