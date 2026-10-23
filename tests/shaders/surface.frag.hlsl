// A custom surface of one color, shaded as Renderer3D shades its meshes.
#include "surface_lighting.hlsl"

cbuffer Surface : register(b1, space3) {
    float4 color;
};

struct Input {
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

float4 main(Input input) : SV_Target0 {
    const float3 shaded = ShadeOpaqueSurface(input.worldPosition, normalize(input.normal), input.position.xy,
                                             color.rgb, 0.0, 1.0, 1.0, float3(0.0, 0.0, 0.0), false);
    return float4(shaded, 1.0);
}
