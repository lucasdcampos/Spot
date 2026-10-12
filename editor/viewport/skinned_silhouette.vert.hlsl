// Places a mesh's triangles in the view, for drawing a flat color where it covers: which entity is under the mouse,
// or where the selection is.
cbuffer Object : register(b0, space1) {
    float4x4 modelViewProjection;
};

StructuredBuffer<float4x4> palettes : register(t0, space0);
cbuffer Skin : register(b1, space1) { uint4 paletteOffset; };

struct Input {
    float3 position : TEXCOORD0;
    uint4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
};

float4 main(Input input) : SV_Position {
    const float4x4 skin = palettes[paletteOffset.x + input.joints.x] * input.weights.x +
                          palettes[paletteOffset.x + input.joints.y] * input.weights.y +
                          palettes[paletteOffset.x + input.joints.z] * input.weights.z +
                          palettes[paletteOffset.x + input.joints.w] * input.weights.w;
    return mul(modelViewProjection, mul(skin, float4(input.position, 1.0)));
}
