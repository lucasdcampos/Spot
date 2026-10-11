cbuffer View : register(b0, space1) { float4x4 viewProjection; };
struct Input {
    float4 centerAndSize : TEXCOORD0;
    float4 rightAndSoft : TEXCOORD1;
    float4 up : TEXCOORD2;
    float4 color : TEXCOORD3;
    float4 uvRect : TEXCOORD4;
};
struct Output {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 localUv : TEXCOORD2;
    float softDistance : TEXCOORD3;
};
Output main(Input input, uint vertex : SV_VertexID) {
    const float2 corners[6] = {float2(-0.5,0.5),float2(0.5,0.5),float2(0.5,-0.5),
                               float2(-0.5,0.5),float2(0.5,-0.5),float2(-0.5,-0.5)};
    const float2 corner = corners[vertex];
    Output output;
    float3 world = input.centerAndSize.xyz + input.centerAndSize.w * (input.rightAndSoft.xyz * corner.x + input.up.xyz * corner.y);
    output.position = mul(viewProjection, float4(world,1));
    output.localUv = float2(corner.x + 0.5, 0.5 - corner.y);
    output.uv = lerp(input.uvRect.xy,input.uvRect.zw,output.localUv);
    output.color = input.color; output.softDistance = input.rightAndSoft.w;
    return output;
}
